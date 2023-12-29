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
#include <X11/Xaw/BoxP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

/*
 * Class Methods
 */
static void XawBoxChangeManaged(Widget);
static void XawBoxClassInitialize(void);
#ifndef OLDXAW
static void XawBoxExpose(Widget, XEvent*, Region);
#endif
static XtGeometryResult XawBoxGeometryManager(Widget, XtWidgetGeometry*,
					      XtWidgetGeometry*);
static void XawBoxInitialize(Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult XawBoxQueryGeometry(Widget, XtWidgetGeometry*,
					    XtWidgetGeometry*);
static void XawBoxRealize(Widget, Mask*, XSetWindowAttributes*);
static void XawBoxResize(Widget);
static Boolean XawBoxSetValues(Widget, Widget, Widget,
			       ArgList, Cardinal*);

/*
 * Prototypes
 */
static void DoLayout(BoxWidget, unsigned int, unsigned int,
		     Dimension*, Dimension*, Bool);
static Bool TryNewLayout(BoxWidget);

/*
 * Initialization
 */
#ifndef OLDXAW
static XtActionsRec actions[] = {
  {"set-values", XawSetValuesAction},
  {"get-values", XawGetValuesAction},
  {"declare",    XawDeclareAction},
  {"call-proc",  XawCallProcAction},
};
#endif

static XtResource resources[] = {
  {
    XtNhSpace,
    XtCHSpace,
    XtRDimension,
    sizeof(Dimension),
    XtOffsetOf(BoxRec, box.h_space),
    XtRImmediate,
    (XtPointer)4
  },
  {
    XtNvSpace,
    XtCVSpace,
    XtRDimension,
    sizeof(Dimension),
    XtOffsetOf(BoxRec, box.v_space),
    XtRImmediate,
    (XtPointer)4
  },
  {
    XtNorientation,
    XtCOrientation,
    XtROrientation,
    sizeof(XtOrientation),
    XtOffsetOf(BoxRec, box.orientation),
    XtRImmediate,
    (XtPointer)XtorientVertical
  },
#ifndef OLDXAW
  {
    XawNdisplayList,
    XawCDisplayList,
    XawRDisplayList,
    sizeof(XawDisplayList*),
    XtOffsetOf(BoxRec, box.display_list),
    XtRImmediate,
    NULL
  },
#endif
};

BoxClassRec boxClassRec = {
  /* core */
  {
    (WidgetClass)&compositeClassRec,	/* superclass */
    "Box",				/* class_name */
    sizeof(BoxRec),			/* widget_size */
    XawBoxClassInitialize,		/* class_initialize */
    NULL,				/* class_part_init */
    False,				/* class_inited */
    XawBoxInitialize,			/* initialize */
    NULL,				/* initialize_hook */
    XawBoxRealize,			/* realize */
#ifndef OLDXAW
    actions,				/* actions */
    XtNumber(actions),			/* num_actions */
#else
    NULL,				/* actions */
    0,					/* num_actions */
#endif
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    NULL,				/* destroy */
    XawBoxResize,			/* resize */
#ifndef OLDXAW
    XawBoxExpose,			/* expose */
#else
    NULL,				/* expose */
#endif
    XawBoxSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XawBoxQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* composite */
  {
    XawBoxGeometryManager,		/* geometry_manager */
    XawBoxChangeManaged,		/* change_managed */
    XtInheritInsertChild,		/* insert_child */
    XtInheritDeleteChild,		/* delete_child */
    NULL,				/* extension */
  },
  /* box */
  {
    NULL,				/* extension */
  },
};

WidgetClass boxWidgetClass = (WidgetClass)&boxClassRec;

/*
 * Do a layout, either actually assigning positions, or just calculating size.
 * Returns minimum width and height that will preserve the same layout.
 */
static void
DoLayout(BoxWidget bbw, unsigned int width, unsigned int height,
	 Dimension *reply_width, Dimension *reply_height, Bool position)
{
    Boolean vbox = (bbw->box.orientation == XtorientVertical);
    Cardinal  i;
    Dimension w, h;	/* Width and height needed for box		*/
    Dimension lw, lh;	/* Width and height needed for current line	*/
    Dimension bw, bh;	/* Width and height needed for current widget	*/
    Dimension h_space;  /* Local copy of bbw->box.h_space		*/
    Widget widget;	/* Current widget				*/
    unsigned int num_mapped_children = 0;

    /* Box width and height */
    h_space = bbw->box.h_space;

    w = 0;
    for (i = 0; i < bbw->composite.num_children; i++) {
	if (XtIsManaged(bbw->composite.children[i])
	    && bbw->composite.children[i]->core.width > w)
	    w = bbw->composite.children[i]->core.width;
    }
    w = (Dimension)(w + h_space);
    if (w > width)
	width = w;
    h = bbw->box.v_space;

    /* Line width and height */
    lh = 0;
    lw = h_space;

    for (i = 0; i < bbw->composite.num_children; i++) {
	widget = bbw->composite.children[i];
	if (widget->core.managed) {
	    if (widget->core.mapped_when_managed)
		num_mapped_children++;
	    /* Compute widget width */
	    bw = (Dimension)(XtWidth(widget) + (XtBorderWidth(widget)<<1) + h_space);
	    if ((Dimension)(lw + bw) > width) {
		if (lw > h_space) {
		    /* At least one widget on this line, and
		     * can't fit any more.  Start new line if vbox
		     */
		    AssignMax(w, lw);
		    if (vbox) {
			h = (Dimension)(h + (lh + bbw->box.v_space));
			lh = 0;
			lw = h_space;
		    }
		}
		else if (!position) {
		    /* too narrow for this widget; we'll assume we can grow */
		    DoLayout(bbw, (unsigned)(lw + bw), height, reply_width,
			     reply_height, position);
		    return;
		}
	    }
	    if (position && (lw != XtX(widget) || h != XtY(widget))) {
		/* It would be nice to use window gravity, but there isn't
		 * sufficient fine-grain control to nicely handle all
		 * situations (e.g. when only the height changes --
		 * a common case).  Explicit unmapping is a cheap hack
		 * to speed things up & avoid the visual jitter as
		 * things slide around.
		 *
		 * %%% perhaps there should be a client resource to
		 * control this.  If so, we'll have to optimize to
		 * perform the moves from the correct end so we don't
		 * force extra exposures as children occlude each other.
		 */
		if (XtIsRealized(widget) && widget->core.mapped_when_managed)
		XUnmapWindow( XtDisplay(widget), XtWindow(widget));
		XtMoveWidget(widget, (Position)lw, (Position)h);
	    }
	    lw = (Dimension)(lw + bw);
	    bh = (Dimension)(XtHeight(widget) + (XtBorderWidth(widget) << 1));
	    AssignMax(lh, bh);
	}
    }

    if (!vbox && width && lw > width && lh < height) {
	/* reduce width if too wide and height not filled */
	Dimension sw = lw, sh = lh;
	Dimension width_needed = (Dimension)width;
	XtOrientation orientation = bbw->box.orientation;

	bbw->box.orientation = XtorientVertical;
	while (sh < height && sw > width) {
	    width_needed = sw;
	    DoLayout(bbw, (unsigned)(sw-1), height, &sw, &sh, False);
	}
	if (sh < height)
	  width_needed = sw;
	if (width_needed != lw) {
	    DoLayout(bbw, width_needed, height,
		     reply_width, reply_height, position);
	    bbw->box.orientation = orientation;
	    return;
	}
	bbw->box.orientation = orientation;
    }
    if (vbox && (width < w || width < lw)) {
	AssignMax(w, lw);
	DoLayout(bbw, w, height, reply_width, reply_height, position);
	return;
    }
     if (position && XtIsRealized((Widget)bbw)) {
	if (bbw->composite.num_children == num_mapped_children)
	    XMapSubwindows(XtDisplay((Widget)bbw), XtWindow((Widget)bbw));
	else {
	    int ii = (int)bbw->composite.num_children;
	    Widget *childP = bbw->composite.children;

	    for (; ii > 0; childP++, ii--)
		if (XtIsRealized(*childP) && XtIsManaged(*childP)
		    && (*childP)->core.mapped_when_managed)
		    XtMapWidget(*childP);
	}
    }

    /* Finish off last line */
    if (lw > h_space) {
	AssignMax(w, lw);
        h = (Dimension)(h + (lh + bbw->box.v_space));
    }

    *reply_width = Max(w, 1);
    *reply_height = Max(h, 1);
}

/*
 * Calculate preferred size, given constraining box, caching it in the widget
 */
static XtGeometryResult
XawBoxQueryGeometry(Widget widget, XtWidgetGeometry *constraint,
		    XtWidgetGeometry *preferred)
{
    BoxWidget w = (BoxWidget)widget;
    Dimension width;
    Dimension preferred_width = w->box.preferred_width;
    Dimension preferred_height = w->box.preferred_height;

    constraint->request_mode &= CWWidth | CWHeight;

    if (constraint->request_mode == 0)
	/* parent isn't going to change w or h, so nothing to re-compute */
    return (XtGeometryYes);

    if (constraint->request_mode == w->box.last_query_mode
	&& (!(constraint->request_mode & CWWidth)
	  || constraint->width == w->box.last_query_width)
	&& (!(constraint->request_mode & CWHeight)
	  || constraint->height == w->box.last_query_height)) {
	/* same query; current preferences are still valid */
	preferred->request_mode = CWWidth | CWHeight;
	preferred->width = preferred_width;
	preferred->height = preferred_height;
	if (constraint->request_mode == (CWWidth | CWHeight)
	    && constraint->width == preferred_width
	    && constraint->height == preferred_height)
	    return (XtGeometryYes);
	else
	    return (XtGeometryAlmost);
    }

    /* else gotta do it the long way...
       I have a preference for tall and narrow, so if my width is
       constrained, I'll accept it; otherwise, I'll compute the minimum
       width that will fit me within the height constraint */

    w->box.last_query_mode = constraint->request_mode;
    w->box.last_query_width = constraint->width;
    w->box.last_query_height= constraint->height;

    if (constraint->request_mode & CWWidth)
	width = constraint->width;
    else { /* if (constraint->request_mode & CWHeight) */
	   /* let's see if I can become any narrower */
	width = 0;
	constraint->width = 65535;
    }

    /* height is currently ignored by DoLayout.
       height = (constraint->request_mode & CWHeight) ? constraint->height
		       : *preferred_height;
     */
    DoLayout(w, width, 0, &preferred_width, &preferred_height, False);

    if (constraint->request_mode & CWHeight
	&& preferred_height > constraint->height) {
	/* find minimum width for this height */
	if (preferred_width <= constraint->width) {
	    width = preferred_width;
	    do { /* find some width big enough to stay within this height */
		if (width > (constraint->width >> 1)) /* avoid short int overflow */
		    width = constraint->width;
		else
		    width = (Dimension)(width << 1);
		DoLayout(w, width, 0, &preferred_width, &preferred_height, False);
	    } while (preferred_height > constraint->height
		     && width < constraint->width);
	    if (width != constraint->width) {
		do { /* find minimum width */
		    width = preferred_width;
		    DoLayout(w, (unsigned)(preferred_width - 1), 0,
			     &preferred_width, &preferred_height, False);
		} while (preferred_height < constraint->height);
		/* one last time */
		DoLayout(w, width, 0, &preferred_width, &preferred_height, False);
	    }
	}
    }

    preferred->request_mode = CWWidth | CWHeight;
    preferred->width = w->box.preferred_width = preferred_width;
    preferred->height = w->box.preferred_height = preferred_height;

    if (constraint->request_mode == (CWWidth|CWHeight)
	&& constraint->width == preferred_width
	&& constraint->height == preferred_height)
	return (XtGeometryYes);

    return (XtGeometryAlmost);
}

/*
 * Actually layout the box
 */
static void
XawBoxResize(Widget w)
{
    Dimension tmp;

    DoLayout((BoxWidget)w, XtWidth(w), XtHeight(w), &tmp, &tmp, True);
}

/*
 * Try to do a new layout within the current width and height;
 * if that fails try to resize and do it within the box returned
 * by XawBoxQueryGeometry
 *
 * TryNewLayout just says if it's possible, and doesn't actually move the kids
 */
static Bool
TryNewLayout(BoxWidget bbw)
{
    Dimension 	preferred_width, preferred_height;
    Dimension	proposed_width, proposed_height;
    int		iterations;

    DoLayout(bbw, bbw->core.width, bbw->core.height,
	     &preferred_width, &preferred_height, False);

    /* at this point, preferred_width is guaranteed to not be greater
       than bbw->core.width unless some child is larger, so there's no
       point in re-computing another layout */

    if (XtWidth(bbw) == preferred_width && XtHeight(bbw) == preferred_height)
	return (True);

    /* let's see if our parent will go for a new size */
    iterations = 0;
    proposed_width = preferred_width;
    proposed_height = preferred_height;
    do {
	switch (XtMakeResizeRequest((Widget)bbw,proposed_width,proposed_height,
				     &proposed_width, &proposed_height)) {
	    case XtGeometryYes:
		return (True);
	    case XtGeometryNo:
		if (iterations > 0)
		    /* protect from malicious parents who change their minds */
		    DoLayout(bbw, bbw->core.width, bbw->core.height,
			     &preferred_width, &preferred_height, False);
		if (preferred_width <= XtWidth(bbw)
		    && preferred_height <= XtHeight(bbw))
		    return (True);
		else
		    return (False);
	    case XtGeometryAlmost:
		if (proposed_height >= preferred_height &&
		    proposed_width >= preferred_width) {
		    /*
		     * Take it, and assume the parent knows what it is doing.
		     *
		     * The parent must accept this since it was returned in
		     * almost.
		     */
		    (void)XtMakeResizeRequest((Widget)bbw,
					       proposed_width, proposed_height,
					       &proposed_width, &proposed_height);
		    return (True);
		}
		else if (proposed_width != preferred_width) {
		    /* recalc bounding box; height might change */
		    DoLayout(bbw, proposed_width, 0,
			     &preferred_width, &preferred_height, False);
		    proposed_height = preferred_height;
		}
		else {	/* proposed_height != preferred_height */
		    XtWidgetGeometry constraints, reply;

		    constraints.request_mode = CWHeight;
		    constraints.height = proposed_height;
		    (void)XawBoxQueryGeometry((Widget)bbw, &constraints, &reply);
		    proposed_width = preferred_width;
		}
		/*FALLTHROUGH*/
	    default:
		break;
	}
	iterations++;
    } while (iterations < 10);

    return (False);
}

/*
 * Geometry Manager
 *
 * 'reply' is unused; we say only yeay or nay, never almost.
 */
/*ARGSUSED*/
static XtGeometryResult
XawBoxGeometryManager(Widget w, XtWidgetGeometry *request,
		      XtWidgetGeometry *reply _X_UNUSED)
{
    Dimension	width, height, borderWidth;
    BoxWidget bbw;

    /* Position request always denied */
    if (((request->request_mode & CWX) && request->x != XtX(w))
	|| ((request->request_mode & CWY) && request->y != XtY(w)))
        return (XtGeometryNo);

    /* Size changes must see if the new size can be accommodated */
    if (request->request_mode & (CWWidth | CWHeight | CWBorderWidth)) {
	/* Make all three fields in the request valid */
	if ((request->request_mode & CWWidth) == 0)
	    request->width = XtWidth(w);
	if ((request->request_mode & CWHeight) == 0)
	    request->height = XtHeight(w);
        if ((request->request_mode & CWBorderWidth) == 0)
	    request->border_width = XtBorderWidth(w);

	/* Save current size and set to new size */
      width = XtWidth(w);
      height = XtHeight(w);
      borderWidth = XtBorderWidth(w);
      XtWidth(w) = request->width;
      XtHeight(w) = request->height;
      XtBorderWidth(w) = request->border_width;

      /* Decide if new layout works:
	 (1) new widget is smaller,
	 (2) new widget fits in existing Box,
	 (3) Box can be expanded to allow new widget to fit
      */

	bbw = (BoxWidget) w->core.parent;

	if (TryNewLayout(bbw)) {
	    /* Fits in existing or new space, relayout */
	    (*XtClass((Widget)bbw)->core_class.resize)((Widget)bbw);
	    return (XtGeometryYes);
	}
	else {
	    /* Cannot satisfy request, change back to original geometry */
	    XtWidth(w) = width;
	    XtHeight(w) = height;
	    XtBorderWidth(w) = borderWidth;
	    return (XtGeometryNo);
	}
    }

    /* Any stacking changes don't make a difference, so allow if that's all */
    return (XtGeometryYes);
}

static void
XawBoxChangeManaged(Widget w)
{
    /* Reconfigure the box */
    (void)TryNewLayout((BoxWidget)w);
    XawBoxResize(w);
}

static void
XawBoxClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtAddConverter(XtRString, XtROrientation, XmuCvtStringToOrientation,
		   NULL, 0);
    XtSetTypeConverter(XtROrientation, XtRString, XmuCvtOrientationToString,
		       NULL, 0, XtCacheNone, NULL);
}

/*ARGSUSED*/
static void
XawBoxInitialize(Widget request _X_UNUSED, Widget cnew,
		 ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    BoxWidget newbbw = (BoxWidget)cnew;

    newbbw->box.last_query_mode = CWWidth | CWHeight;
    newbbw->box.last_query_width = newbbw->box.last_query_height = 0;
    newbbw->box.preferred_width = Max(newbbw->box.h_space, 1);
    newbbw->box.preferred_height = Max(newbbw->box.v_space, 1);

    if (XtWidth(newbbw) == 0)
	XtWidth(newbbw) = newbbw->box.preferred_width;

    if (XtHeight(newbbw) == 0)
	XtHeight(newbbw) = newbbw->box.preferred_height;
}

static void
XawBoxRealize(Widget w, Mask *valueMask, XSetWindowAttributes *attributes)
{
#ifndef OLDXAW
    XawPixmap *pixmap;
#endif

    XtCreateWindow(w, InputOutput, (Visual *)CopyFromParent,
		   *valueMask, attributes);

#ifndef OLDXAW
    if (w->core.background_pixmap > XtUnspecifiedPixmap) {
	pixmap = XawPixmapFromXPixmap(w->core.background_pixmap, XtScreen(w),
				      w->core.colormap, (int)w->core.depth);
	if (pixmap && pixmap->mask)
	    XawReshapeWidget(w, pixmap);
    }
#endif
}

/*ARGSUSED*/
static Boolean
XawBoxSetValues(Widget current _X_UNUSED, Widget request _X_UNUSED, Widget cnew _X_UNUSED,
		ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
     /* need to relayout if h_space or v_space change */
#ifndef OLDXAW
    BoxWidget b_old = (BoxWidget)current;
    BoxWidget b_new = (BoxWidget)cnew;

    if (b_old->core.background_pixmap != b_new->core.background_pixmap) {
	XawPixmap *opix, *npix;

	opix = XawPixmapFromXPixmap(b_old->core.background_pixmap,
				    XtScreen(b_old), b_old->core.colormap,
				    (int)b_old->core.depth);
	npix = XawPixmapFromXPixmap(b_new->core.background_pixmap,
				    XtScreen(b_new), b_new->core.colormap,
				    (int)b_new->core.depth);
	if ((npix && npix->mask) || (opix && opix->mask))
	    XawReshapeWidget(cnew, npix);
    }
#endif /* OLDXAW */

  return (False);
}

#ifndef OLDXAW
static void
XawBoxExpose(Widget w, XEvent *event, Region region)
{
    BoxWidget xaw = (BoxWidget)w;

    if (xaw->box.display_list)
	XawRunDisplayList(w, xaw->box.display_list, event, region);
}
#endif /* OLDXAW */
