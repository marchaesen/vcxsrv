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
 * This widget is a trivial clipping widget.  It is typically used with a
 * panner or scrollbar to navigate.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/PortholeP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

/*
 * Class Methods
 */
static void XawPortholeChangeManaged(Widget);
static XtGeometryResult XawPortholeGeometryManager(Widget, XtWidgetGeometry*,
						   XtWidgetGeometry*);
static XtGeometryResult XawPortholeQueryGeometry(Widget, XtWidgetGeometry*,
						 XtWidgetGeometry*);
static void XawPortholeRealize(Widget, Mask*, XSetWindowAttributes*);
static void XawPortholeResize(Widget);

/*
 * Prototypes
 */
static Widget find_child(PortholeWidget);
static void layout_child(PortholeWidget, Widget, XtWidgetGeometry*,
			 Position*, Position*, Dimension*, Dimension*);
static void SendReport(PortholeWidget, unsigned int);

/*
 * Initialization
 */
#define offset(field)	XtOffsetOf(PortholeRec, porthole.field)
static XtResource resources[] = {
  {
    XtNreportCallback,
    XtCReportCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(report_callbacks),
    XtRCallback,
    NULL
  },
};
#undef offset

#define Superclass	(&compositeClassRec)
PortholeClassRec portholeClassRec = {
  /* core */
  {
    (WidgetClass)Superclass,		/* superclass */
    "Porthole",				/* class_name */
    sizeof(PortholeRec),		/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    NULL,				/* initialize */
    NULL,				/* initialize_hook */
    XawPortholeRealize,			/* realize */
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
    XawPortholeResize,			/* resize */
    NULL,				/* expose */
    NULL,				/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XawPortholeQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* composite */
  {
    XawPortholeGeometryManager,		/* geometry_manager */
    XawPortholeChangeManaged,		/* change_managed */
    XtInheritInsertChild,		/* insert_child */
    XtInheritDeleteChild,		/* delete_child */
    NULL,				/* extension */
  },
  { /* porthole */
    NULL,				/* extension */
  },
};

WidgetClass portholeWidgetClass = (WidgetClass)&portholeClassRec;

/*
 * Implementation
 */
static Widget
find_child(PortholeWidget pw)
{
    Widget *children;
    unsigned int i;

    /*
     * Find the managed child on which we should operate.  Ignore multiple
     * managed children
     */
    for (i = 0, children = pw->composite.children;
	 i < pw->composite.num_children; i++, children++)
	if (XtIsManaged(*children))
	    return (*children);

    return (NULL);
}

static void
SendReport(PortholeWidget pw, unsigned int changed)
{
    Widget child = find_child(pw);

    if (pw->porthole.report_callbacks && child) {
	XawPannerReport prep;

	prep.changed = changed;
	prep.slider_x = (Position)(-XtX(child));	/* porthole is "inner" */
	prep.slider_y = (Position)(-XtY(child));	/* child is outer since it is larger */
	prep.slider_width = XtWidth(pw);
	prep.slider_height = XtHeight(pw);
	prep.canvas_width = XtWidth(child);
	prep.canvas_height = XtHeight(child);
	XtCallCallbackList((Widget)pw, pw->porthole.report_callbacks,
			   (XtPointer)&prep);
    }
}

static void
layout_child(PortholeWidget pw, Widget child, XtWidgetGeometry *geomp,
	     Position *xp, Position *yp, Dimension *widthp, Dimension *heightp)
{
    Position minx, miny;

    *xp = XtX(child);			/* default to current values */
    *yp = XtY(child);
    *widthp = XtWidth(child);
    *heightp = XtHeight(child);
    if (geomp) {			/* mix in any requested changes */
	if (geomp->request_mode & CWX)
	    *xp = geomp->x;
	if (geomp->request_mode & CWY)
	    *yp = geomp->y;
	if (geomp->request_mode & CWWidth)
	    *widthp = geomp->width;
	if (geomp->request_mode & CWHeight)
	    *heightp = geomp->height;
    }

    /*
     * Make sure that the child is at least as large as the porthole; there
     * is no maximum size
     */
    if (*widthp < XtWidth(pw)) *widthp = XtWidth(pw);
    if (*heightp < XtHeight(pw)) *heightp = XtHeight(pw);

    /*
     * Make sure that the child is still on the screen.  Note that this must
     * be done *after* the size computation so that we know where to put it
     */
    minx = (Position)(XtWidth(pw) - *widthp);
    miny = (Position)(XtHeight(pw) - *heightp);

    if (*xp < minx)
	*xp = minx;
    if (*yp < miny)
	*yp = miny;

    if (*xp > 0)
	*xp = 0;
    if (*yp > 0)
	*yp = 0;
}

static void
XawPortholeRealize(Widget gw, Mask *valueMask, XSetWindowAttributes *attr)
{
    attr->bit_gravity = NorthWestGravity;
    *valueMask |= CWBitGravity;

    if (XtWidth(gw) < 1)
	XtWidth(gw) = 1;
    if (XtHeight(gw) < 1)
	XtHeight(gw) = 1;
    (*portholeWidgetClass->core_class.superclass->core_class.realize)
	(gw, valueMask, attr);
}

static void
XawPortholeResize(Widget gw)
{
    PortholeWidget pw = (PortholeWidget)gw;
    Widget child = find_child(pw);

    /*
     * If we have a child, we need to make sure that it is at least as big
     * as we are and in the right place
     */
    if (child) {
	Position x, y;
	Dimension width, height;

	layout_child(pw, child, NULL, &x, &y, &width, &height);
	XtConfigureWidget(child, x, y, width, height, 0);
    }

    SendReport(pw, XawPRCanvasWidth | XawPRCanvasHeight);
}

static XtGeometryResult
XawPortholeQueryGeometry(Widget gw, XtWidgetGeometry *intended,
			 XtWidgetGeometry *preferred)
{
    PortholeWidget pw = (PortholeWidget)gw;
    Widget child = find_child(pw);

    if (child) {
#define SIZEONLY (CWWidth | CWHeight)
	preferred->request_mode = SIZEONLY;
	preferred->width = XtWidth(child);
	preferred->height = XtHeight(child);

	if ((intended->request_mode & SIZEONLY) == SIZEONLY &&
	    intended->width == preferred->width &&
	    intended->height == preferred->height)
	    return (XtGeometryYes);
	else if (preferred->width == XtWidth(pw) &&
		 preferred->height == XtHeight(pw))
	    return (XtGeometryNo);

	return (XtGeometryAlmost);
#undef SIZEONLY
    }

    return (XtGeometryNo);
}

static XtGeometryResult
XawPortholeGeometryManager(Widget w, XtWidgetGeometry *req,
			   XtWidgetGeometry *reply)
{
    PortholeWidget pw = (PortholeWidget) w->core.parent;
    Widget child = find_child(pw);
    Bool okay = True;

    if (child != w)
	return (XtGeometryNo);

    *reply = *req;			/* assume we'll grant everything */

    if ((req->request_mode & CWBorderWidth) && req->border_width != 0) {
	reply->border_width = 0;
	okay = False;
    }

    layout_child(pw, child, req, &reply->x, &reply->y,
		 &reply->width, &reply->height);

    if ((req->request_mode & CWX) && req->x != reply->x)
	okay = False;
    if ((req->request_mode & CWY) && req->x != reply->x)
	okay = False;
    if ((req->request_mode & CWWidth) && req->width != reply->width)
	okay = False;
    if ((req->request_mode & CWHeight) && req->height != reply->height)
	okay = False;

    /*
     * If we failed on anything, simply return without touching widget
     */
    if (!okay)
	return (XtGeometryAlmost);

    /*
     * If not just doing a query, update widget and send report.  Note that
     * we will often set fields that weren't requested because we want to keep
     * the child visible
     */
    if (!(req->request_mode & XtCWQueryOnly)) {
	unsigned int changed = 0;

	if (XtX(child) != reply->x) {
	    changed |= XawPRSliderX;
	    XtX(child) = reply->x;
	}
	if (XtY(child) != reply->y) {
	    changed |= XawPRSliderY;
	    XtY(child) = reply->y;
	}
	if (XtWidth(child) != reply->width) {
	    changed |= XawPRSliderWidth;
	    XtWidth(child) = reply->width;
	}
	if (XtHeight(child) != reply->height) {
	    changed |= XawPRSliderHeight;
	    XtHeight(child) = reply->height;
	}
	if (changed)
	    SendReport(pw, changed);
    }

    return (XtGeometryYes);		/* success! */
}

static void
XawPortholeChangeManaged(Widget gw)
{
    PortholeWidget pw = (PortholeWidget)gw;
    Widget child = find_child (pw);	/* ignore extra children */

    if (child) {
	if (!XtIsRealized (gw)) {
	    XtWidgetGeometry geom, retgeom;

	    geom.request_mode = 0;
	    if (XtWidth(pw) == 0) {
		geom.width = XtWidth(child);
		geom.request_mode |= CWWidth;
	    }
	    if (XtHeight(pw) == 0) {
		geom.height = XtHeight(child);
		geom.request_mode |= CWHeight;
	    }
	    if (geom.request_mode &&
		XtMakeGeometryRequest (gw, &geom, &retgeom)
		== XtGeometryAlmost)
		(void)XtMakeGeometryRequest(gw, &retgeom, NULL);
	}

	XtResizeWidget(child, Max(XtWidth(child), XtWidth(pw)),
		       Max(XtHeight(child), XtHeight(pw)), 0);

	SendReport(pw, XawPRAll);
    }
}
