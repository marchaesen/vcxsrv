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
 *
 * Author:  Chris D. Peterson, MIT X Consortium
 */

/*
 * Sme.c - Source code for the generic menu entry
 *
 * Date:    September 26, 1989
 *
 * By:      Chris D. Peterson
 *          MIT X Consortium
 *          kit@expo.lcs.mit.edu
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Cardinals.h>
#include <X11/Xaw/SmeLineP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

/*
 * Class Methods
 */
static void XawSmeLineDestroy(Widget);
static void XawSmeLineInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawSmeLineRedisplay(Widget, XEvent*, Region);
static Boolean XawSmeLineSetValues(Widget, Widget, Widget,
				   ArgList, Cardinal*);

/*
 * Prototypes
 */
static void CreateGC(Widget);
static void DestroyGC(Widget);

/*
 * Initialization
 */
#define offset(field)	XtOffsetOf(SmeLineRec, sme_line.field)
static XtResource resources[] = {
  {
    XtNlineWidth,
    XtCLineWidth,
    XtRDimension,
    sizeof(Dimension),
    offset(line_width),
    XtRImmediate,
    (XtPointer)1
  },
  {
    XtNstipple,
    XtCStipple,
    XtRBitmap,
    sizeof(Pixmap),
    offset(stipple),
    XtRImmediate,
    (XtPointer)XtUnspecifiedPixmap
  },
  {
    XtNforeground,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    offset(foreground),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
};
#undef offset

#define Superclass	(&smeClassRec)
SmeLineClassRec smeLineClassRec = {
  /* rectangle */
  {
    (WidgetClass)Superclass,		/* superclass */
    "SmeLine",				/* class_name */
    sizeof(SmeLineRec),			/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class inited */
    XawSmeLineInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    NULL,				/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    False,				/* compress_motion */
    False,				/* compress_exposure */
    False,				/* compress_enterleave */
    False,				/* visible_interest */
    XawSmeLineDestroy,			/* destroy */
    NULL,				/* resize */
    XawSmeLineRedisplay,		/* expose */
    XawSmeLineSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* intrinsics version */
    NULL,				/* callback offsets */
    NULL,				/* tm_table */
    XtInheritQueryGeometry,		/* query_geometry */
    NULL,				/* display_accelerator */
    NULL,				/* extension */
  },
  /* sme */
  {
    XtInheritHighlight,			/* highlight */
    XtInheritUnhighlight,		/* unhighlight */
    XtInheritNotify,			/* notify */
    NULL,				/* extension */
  },
  /* sme_line */
  {
    NULL,				/* extension */
  }
};

WidgetClass smeLineObjectClass = (WidgetClass)&smeLineClassRec;

/*
 * Implementation
 */
/*ARGSUSED*/
static void
XawSmeLineInitialize(Widget request _X_UNUSED, Widget cnew,
		     ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SmeLineObject entry = (SmeLineObject)cnew;

    if (XtHeight(entry) == 0)
	XtHeight(entry) = entry->sme_line.line_width;

    CreateGC(cnew);
}

/*
 * Function:
 *	CreateGC
 *
 * Parameters:
 *	w - Line entry widget
 *
 * Description:
 *	Creates the GC for the line entry widget.
 *
 * Note:
 *      We can only share the GC if there is no stipple, because
 *	we need to change the stipple origin when drawing
 */
static void
CreateGC(Widget w)
{
    SmeLineObject entry = (SmeLineObject)w;
    XGCValues values;
    XtGCMask mask = GCForeground | GCGraphicsExposures | GCLineWidth;

    values.foreground = entry->sme_line.foreground;
    values.graphics_exposures = False;
    values.line_width = entry->sme_line.line_width;

    if (entry->sme_line.stipple != XtUnspecifiedPixmap) {
	values.stipple = entry->sme_line.stipple;
	values.fill_style = FillStippled;
	mask |= GCStipple | GCFillStyle;

	entry->sme_line.gc = XCreateGC(XtDisplayOfObject(w),
				      RootWindowOfScreen(XtScreenOfObject(w)),
				      mask, &values);
    }
    else
	entry->sme_line.gc = XtGetGC(w, mask, &values);
}

static void
XawSmeLineDestroy(Widget w)
{
    DestroyGC(w);
}

static void
DestroyGC(Widget w)
{
    SmeLineObject entry = (SmeLineObject)w;

    if (entry->sme_line.stipple != XtUnspecifiedPixmap)
	XFreeGC(XtDisplayOfObject(w), entry->sme_line.gc);
    else
	XtReleaseGC(w, entry->sme_line.gc);
}

/*ARGSUSED*/
static void
XawSmeLineRedisplay(Widget w, XEvent *event _X_UNUSED, Region region _X_UNUSED)
{
    SmeLineObject entry = (SmeLineObject)w;
    int y = XtY(w) + (((int)XtHeight(w) - entry->sme_line.line_width) >> 1);

    if (entry->sme_line.stipple != XtUnspecifiedPixmap)
	XSetTSOrigin(XtDisplayOfObject(w), entry->sme_line.gc, 0, y);

    XFillRectangle(XtDisplayOfObject(w), XtWindowOfObject(w),
		   entry->sme_line.gc, XtX(w), y,
		   XtWidth(w), entry->sme_line.line_width);
}

/*
 * Function:
 *	XawSmeLineSetValues
 *
 * Parameters:
 *	current - current state of the widget
 *	request - what was requested
 *	cnew	- what the widget will become
 *
 * Description:
 *	Relayout the menu when one of the resources is changed.
 */
/*ARGSUSED*/
static Boolean
XawSmeLineSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SmeLineObject entry = (SmeLineObject)cnew;
    SmeLineObject old_entry = (SmeLineObject)current;

    if (entry->sme_line.line_width != old_entry->sme_line.line_width &&
	entry->sme_line.stipple != old_entry->sme_line.stipple) {
	DestroyGC(current);
	CreateGC(cnew);
	return (True);
    }

    return (False);
}
