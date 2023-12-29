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
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xaw/FormP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

/*
 * Class Methods
 */
static void XawFormChangeManaged(Widget);
static void XawFormClassInitialize(void);
static void XawFormClassPartInitialize(WidgetClass);
static void XawFormConstraintInitialize(Widget, Widget, ArgList, Cardinal*);
static Boolean XawFormConstraintSetValues(Widget, Widget, Widget,
					  ArgList, Cardinal*);
static XtGeometryResult XawFormGeometryManager(Widget, XtWidgetGeometry*,
					       XtWidgetGeometry*);
static void XawFormInitialize(Widget, Widget, ArgList, Cardinal*);
#ifndef OLDXAW
static void XawFormRealize(Widget, Mask*, XSetWindowAttributes*);
static void XawFormRedisplay(Widget, XEvent*, Region);
#endif
static XtGeometryResult XawFormQueryGeometry(Widget, XtWidgetGeometry*,
					     XtWidgetGeometry*);
static void XawFormResize(Widget);
static Boolean XawFormSetValues(Widget, Widget, Widget,	ArgList, Cardinal*);
static Boolean Layout(FormWidget, unsigned int, unsigned int, Bool);

/*
 * Prototypes
 */
static void _CvtStringToEdgeType(XrmValuePtr, Cardinal*,
				 XrmValuePtr, XrmValuePtr);
static Bool ChangeFormGeometry(Widget, Bool, unsigned int, unsigned int,
			       Dimension*, Dimension*);
Boolean CvtEdgeTypeToString(Display*, XrmValuePtr, Cardinal*,
			    XrmValuePtr, XrmValuePtr, XtPointer*);
static void LayoutChild(Widget);
static int TransformCoord(int, unsigned int, unsigned int, XtEdgeType);
static void ResizeChildren(Widget);

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

static XrmQuark	QchainLeft, QchainRight, QchainTop, QchainBottom, Qrubber;

#define default_value	-99999
#define Offset(field) XtOffsetOf(FormRec, form.field)
static XtResource resources[] = {
  {
    XtNdefaultDistance,
    XtCThickness,
    XtRInt,
    sizeof(int),
    Offset(default_spacing),
    XtRImmediate,
    (XtPointer)4
  },
#ifndef OLDXAW
  {
    XawNdisplayList,
    XawCDisplayList,
    XawRDisplayList,
    sizeof(XawDisplayList*),
    Offset(display_list),
    XtRImmediate,
    NULL
  },
#endif
};
#undef Offset

#define defEdge		XtRubber

#define Offset(field) XtOffsetOf(FormConstraintsRec, form.field)
static XtResource formConstraintResources[] = {
  {
    XtNtop,
    XtCEdge,
    XtREdgeType,
    sizeof(XtEdgeType),
    Offset(top),
    XtRImmediate,
    (XtPointer)defEdge
  },
  {
    XtNbottom,
    XtCEdge,
    XtREdgeType,
    sizeof(XtEdgeType),
    Offset(bottom),
    XtRImmediate,
    (XtPointer)defEdge
  },
  {
    XtNleft,
    XtCEdge,
    XtREdgeType,
    sizeof(XtEdgeType),
    Offset(left),
    XtRImmediate,
    (XtPointer)defEdge
  },
  {
    XtNright,
    XtCEdge,
    XtREdgeType,
    sizeof(XtEdgeType),
    Offset(right),
    XtRImmediate,
    (XtPointer)defEdge
  },
  {
    XtNhorizDistance,
    XtCThickness,
    XtRInt,
    sizeof(int),
    Offset(dx),
    XtRImmediate,
    (XtPointer)default_value
  },
  {
    XtNfromHoriz,
    XtCWidget,
    XtRWidget,
    sizeof(Widget),
    Offset(horiz_base),
    XtRWidget,
    NULL
  },
  {
    XtNvertDistance,
    XtCThickness,
    XtRInt,
    sizeof(int),
    Offset(dy),
    XtRImmediate,
    (XtPointer)default_value
  },
  {
    XtNfromVert,
    XtCWidget,
    XtRWidget,
    sizeof(Widget),
    Offset(vert_base),
    XtRWidget,
    NULL
  },
  {
    XtNresizable,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    Offset(allow_resize),
    XtRImmediate,
    (XtPointer)False
  },
};
#undef Offset

FormClassRec formClassRec = {
  /* core */
  {
    (WidgetClass)&constraintClassRec,	/* superclass */
    "Form",				/* class_name */
    sizeof(FormRec),			/* widget_size */
    XawFormClassInitialize,		/* class_initialize */
    XawFormClassPartInitialize,		/* class_part_init */
    False,				/* class_inited */
    XawFormInitialize,			/* initialize */
    NULL,				/* initialize_hook */
#ifndef OLDXAW
    XawFormRealize,			/* realize */
    actions,				/* actions */
    XtNumber(actions),			/* num_actions */
#else
    XtInheritRealize,			/* realize */
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
    XawFormResize,			/* resize */
#ifndef OLDXAW
    XawFormRedisplay,			/* expose */
#else
    XtInheritExpose,			/* expose */
#endif
    XawFormSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XawFormQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* composite */
  {
    XawFormGeometryManager,		/* geometry_manager */
    XawFormChangeManaged,		/* change_managed */
    XtInheritInsertChild,		/* insert_child */
    XtInheritDeleteChild,		/* delete_child */
    NULL,				/* extension */
  },
  /* constraint */
  {
    formConstraintResources,		/* subresourses */
    XtNumber(formConstraintResources),	/* subresource_count */
    sizeof(FormConstraintsRec),		/* constraint_size */
    XawFormConstraintInitialize,	/* initialize */
    NULL,				/* destroy */
    XawFormConstraintSetValues,		/* set_values */
    NULL,				/* extension */
  },
  /* form */
  {
    Layout,				/* layout */
#ifndef OLDXAW
    NULL,
#endif
  },
};

WidgetClass formWidgetClass = (WidgetClass)&formClassRec;

/*
 * Implementation
 */
#ifndef OLDXAW
static void
XawFormRealize(Widget w, Mask *mask, XSetWindowAttributes *attr)
{
    XawPixmap *pixmap;

    (*formWidgetClass->core_class.superclass->core_class.realize)(w, mask, attr);

    if (w->core.background_pixmap > XtUnspecifiedPixmap) {
	pixmap = XawPixmapFromXPixmap(w->core.background_pixmap, XtScreen(w),
				      w->core.colormap, (int)w->core.depth);
	if (pixmap && pixmap->mask)
	    XawReshapeWidget(w, pixmap);
    }
}

static void
XawFormRedisplay(Widget w, XEvent *event, Region region)
{
    FormWidget xaw = (FormWidget)w;

    if (xaw->form.display_list)
	XawRunDisplayList(w, xaw->form.display_list, event, region);
}
#endif

/*ARGSUSED*/
static void
_CvtStringToEdgeType(XrmValuePtr args _X_UNUSED, Cardinal *num_args _X_UNUSED,
		     XrmValuePtr fromVal, XrmValuePtr toVal)
{
    static XtEdgeType edgeType;
    XrmQuark q;
    char name[12];

    XmuNCopyISOLatin1Lowered(name, (char*)fromVal->addr, sizeof(name));
    q = XrmStringToQuark(name);

    if (q == QchainLeft)
	edgeType = XtChainLeft;
    else if (q == QchainRight)
	edgeType = XtChainRight;
    else if (q == QchainTop)
	edgeType = XtChainTop;
    else if (q == QchainBottom)
	edgeType = XtChainBottom;
    else if (q == Qrubber)
	edgeType = XtRubber;
    else {
	XtStringConversionWarning(fromVal->addr, XtREdgeType);
	toVal->size = 0;
	toVal->addr = NULL;
	return;
    }

    toVal->size = sizeof(XtEdgeType);
    toVal->addr = (XPointer)&edgeType;
}

/*ARGSUSED*/
Boolean
CvtEdgeTypeToString(Display *dpy, XrmValuePtr args _X_UNUSED, Cardinal *num_args _X_UNUSED,
		    XrmValuePtr fromVal, XrmValuePtr toVal, XtPointer *data _X_UNUSED)
{
    static String buffer;
    Cardinal size;

    switch (*(XtEdgeType *)fromVal->addr) {
	case XtChainLeft:
	    buffer = XtEchainLeft;
	    break;
	case XtChainRight:
	    buffer = XtEchainRight;
	    break;
	case XtChainTop:
	    buffer = XtEchainTop;
	    break;
	case XtChainBottom:
	    buffer = XtEchainBottom;
	    break;
	case XtRubber:
	    buffer = XtErubber;
	    break;
	default:
	    XawTypeToStringWarning(dpy, XtREdgeType);
	    toVal->addr = NULL;
	    toVal->size = 0;
	    return (False);
    }

    size = (Cardinal)strlen(buffer) + 1;
    if (toVal->addr != NULL) {
	if (toVal->size < size)	{
	    toVal->size = size;
	    return (False);
	}
	strcpy((char *)toVal->addr, buffer);
    }
    else
	toVal->addr = (XPointer)buffer;
    toVal->size = sizeof(String);

    return (True);
}

static void
XawFormClassInitialize(void)
{
    static XtConvertArgRec parentCvtArgs[] = {
	{XtBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.parent),
	 sizeof(Widget)}
    };

    char name[12];

    XawInitializeWidgetSet();
    XmuNCopyISOLatin1Lowered(name, XtEchainLeft, sizeof(name));
    QchainLeft = XrmStringToQuark(name);
    XmuNCopyISOLatin1Lowered(name, XtEchainRight, sizeof(name));
    QchainRight = XrmStringToQuark(name);
    XmuNCopyISOLatin1Lowered(name, XtEchainTop, sizeof(name));
    QchainTop = XrmStringToQuark(name);
    XmuNCopyISOLatin1Lowered(name, XtEchainBottom, sizeof(name));
    QchainBottom = XrmStringToQuark(name);
    XmuNCopyISOLatin1Lowered(name, XtErubber, sizeof(name));
    Qrubber = XrmStringToQuark(name);

    XtAddConverter(XtRString, XtREdgeType, _CvtStringToEdgeType, NULL, 0);
    XtSetTypeConverter(XtREdgeType, XtRString, CvtEdgeTypeToString,
		       NULL, 0, XtCacheNone, NULL);
    XtSetTypeConverter(XtRString, XtRWidget, XmuNewCvtStringToWidget,
		       parentCvtArgs, XtNumber(parentCvtArgs), XtCacheNone,
		       NULL);
    XtSetTypeConverter(XtRWidget, XtRString, XmuCvtWidgetToString,
		       NULL, 0, XtCacheNone, NULL);
}

static void
XawFormClassPartInitialize(WidgetClass cclass)
{
    FormWidgetClass c = (FormWidgetClass)cclass;
    FormWidgetClass super = (FormWidgetClass)c->core_class.superclass;

    if (c->form_class.layout == XtInheritLayout)
	c->form_class.layout = super->form_class.layout;
}

/*ARGSUSED*/
static void
XawFormInitialize(Widget request _X_UNUSED, Widget cnew,
		  ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    FormWidget fw = (FormWidget)cnew;

    fw->form.old_width = fw->form.old_height = 0;
    fw->form.no_refigure = False;
    fw->form.needs_relayout = False;
    fw->form.resize_in_layout = True;
    fw->form.resize_is_no_op = False;
}

/*
 * Function:
 *	ChangeFormGeometry
 *
 * Parameters:
 *	w	   - Form widget
 *	query_only - is only a query?
 *	width	   - new width and height
 *	height	   - ""
 *	ret_width  - actual size the form is allowed to resize to (return)
 *	ret_height - ""
 *
 * Description:
 *	Ask the parent to change the form widget's geometry.
 *
 * Returns:
 *	True of children may always be resized
 */
static Bool
ChangeFormGeometry(Widget w, Bool query_only,
		   unsigned int width, unsigned int height,
		   Dimension *ret_width, Dimension *ret_height)
{
    FormWidget fw = (FormWidget)w;
    Boolean always_resize_children;
    XtGeometryResult result;
    XtWidgetGeometry request, return_request;

    /*
     * If we are already at the desired size then there is no need
     * to ask our parent of we can change size
     */
    if (width == XtWidth(fw) && height == XtHeight(fw))
	return (True);

    request.width = (Dimension)width;
    request.height = (Dimension)height;
    request.request_mode = CWWidth | CWHeight;
    if (query_only)
	request.request_mode |= XtCWQueryOnly;

    /*
     * Do no invoke the resize rules if our size changes here
     */
    fw->form.resize_is_no_op = True;

    result = XtMakeGeometryRequest(w, &request, &return_request);
    if (result == XtGeometryAlmost) {
	request = return_request;
	(void)XtMakeGeometryRequest(w, &request, &return_request);
	always_resize_children = False;
    }
    else
	always_resize_children = result == XtGeometryYes;

    fw->form.resize_is_no_op = False;

    if (ret_width != NULL)
	*ret_width = request.width;
    if (ret_height != NULL)
	*ret_height = request.height;

    return (always_resize_children);
}

/*
 * Function:
 *	Layout
 *
 * Parameters:
 *	fw	       - Form widget
 *	width	       - unused
 *	height	       - ""
 *	force_relayout - will force the children to be moved, even if some
 *			 go past the edge of the form
 *
 * Description:
 *	Moves all the children around.
 *
 * Returns:
 *	  True if the children are allowed to move from their
 *	  current locations to the new ones.
 */
/*ARGSUSED*/
static Boolean
Layout(FormWidget fw, unsigned int width _X_UNUSED, unsigned int height _X_UNUSED,
       Bool force_relayout)
{
    int num_children = (int)fw->composite.num_children;
    WidgetList children = fw->composite.children;
    Widget *childP;
    Dimension maxx, maxy;
    Boolean ret_val;

    for (childP = children; childP - children < num_children; childP++) {
	FormConstraints form = (FormConstraints)(*childP)->core.constraints;
	form->form.layout_state = LayoutPending;
    }

    maxx = maxy = 1;
    for (childP = children; childP - children < num_children; childP++) {
	if (XtIsManaged(*childP)) {
	    FormConstraints form;
	    Position x, y;

	    form = (FormConstraints)(*childP)->core.constraints;

	    LayoutChild(*childP);

	    x = (Position)(form->form.new_x + XtWidth(*childP)
		+ (XtBorderWidth(*childP) << 1));
	    if (x > (int)maxx)
		maxx = (Dimension)x;

	    y = (Position)(form->form.new_y + XtHeight(*childP)
		+ (XtBorderWidth(*childP) << 1));
	    if (y > (int)maxy)
		maxy = (Dimension)y;
	}
    }

    fw->form.preferred_width = (maxx = (Dimension)(maxx + fw->form.default_spacing));
    fw->form.preferred_height = (maxy = (Dimension)(maxy + fw->form.default_spacing));

    if (fw->form.resize_in_layout) {
	Boolean always_resize_children;

	always_resize_children =
	    (Boolean)ChangeFormGeometry((Widget)fw, False, maxx, maxy, NULL, NULL);

#ifdef OLDXAW
	fw->form.old_width  = fw->core.width;
	fw->form.old_height = fw->core.height;
#endif

	if (force_relayout)
	    ret_val = True;
	else
	    ret_val = always_resize_children ||
			(XtWidth(fw) >= maxx && XtHeight(fw) >= maxy);

	if (ret_val)
	    ResizeChildren((Widget)fw);
    }
    else
	ret_val = False;

    fw->form.needs_relayout = False;

    return (ret_val);
}

/*
 * Function:
 *	ResizeChildren
 *
 * Parameters:
 *	w - form widget
 *
 * Description:
 *	Resizes all children to new_x and new_y.
 */
static void
ResizeChildren(Widget w)
{
    FormWidget fw = (FormWidget)w;
    int num_children = (int)fw->composite.num_children;
    WidgetList children = fw->composite.children;
    Widget *childP;

    for (childP = children; childP - children < num_children; childP++) {
	FormConstraints form;
	Position x, y;

	if (!XtIsManaged(*childP))
	    continue;

	form = (FormConstraints)(*childP)->core.constraints;

	if (fw->form.old_width && fw->form.old_height) {
	    x = (Position)TransformCoord(form->form.new_x, fw->form.old_width,
			       XtWidth(fw), form->form.left);
	    y = (Position)TransformCoord(form->form.new_y, fw->form.old_height,
			       XtHeight(fw), form->form.top);
	}
	else {
	    x = form->form.new_x;
	    y = form->form.new_y;
	}

	if (fw->form.no_refigure) {
	   /*
	    * I am changing the widget wrapper w/o modifying the window. This is
	    * risky, but I can get away with it since I am the parent of this
	    * widget, and he must ask me for any geometry changes
	    *
	    * The window will be updated when no_refigure is set back to False
	    */
	    XtX(*childP) = x;
	    XtY(*childP) = y;
	}
	else
	    XtMoveWidget(*childP, x, y);
    }
}

static void
LayoutChild(Widget w)
{
    FormConstraints form = (FormConstraints)w->core.constraints;
    Widget ref;

    switch (form->form.layout_state) {
	case LayoutPending:
	    form->form.layout_state = LayoutInProgress;
	    break;
	case LayoutDone:
	    return;
	case LayoutInProgress: {
	    String subs[2];
	    Cardinal num_subs = 2;
	    subs[0] = w->core.name;
	    subs[1] = w->core.parent->core.name;

	    XtAppWarningMsg(XtWidgetToApplicationContext(w),
			    "constraintLoop", "xawFormLayout", "XawToolkitError",
			    "constraint loop detected while laying out "
			    "child '%s' in FormWidget '%s'",
			    subs, &num_subs);
	}   return;
    }

    form->form.new_x = (Position)form->form.dx;
    form->form.new_y = (Position)form->form.dy;
    if ((ref = form->form.horiz_base) != NULL) {
	FormConstraints ref_form = (FormConstraints)ref->core.constraints;

	LayoutChild(ref);
	form->form.new_x = (Position)(form->form.new_x
				      + (ref_form->form.new_x
				      + XtWidth(ref)
				      + (XtBorderWidth(ref) << 1)));
    }
    if ((ref = form->form.vert_base) != NULL) {
	FormConstraints ref_form = (FormConstraints)ref->core.constraints;

	LayoutChild(ref);
	form->form.new_y = (Position)(form->form.new_y
				      + (ref_form->form.new_y
				      + XtHeight(ref)
				      + (XtBorderWidth(ref) << 1)));
    }

    form->form.layout_state = LayoutDone;
}

static int
TransformCoord(int loc, unsigned int old, unsigned int cnew, XtEdgeType type)
{
    if (type == XtRubber) {
	if ((int)old > 0)
	    loc = (int)(loc * ((double)cnew / (double)old));
    }
    else if (type == XtChainBottom || type == XtChainRight)
	loc += (int)cnew - (int)old;

    return (loc);
}

static void
XawFormResize(Widget w)
{
    FormWidget fw = (FormWidget)w;
    WidgetList children = fw->composite.children;
    int num_children = (int)fw->composite.num_children;
    Widget *childP;
    int x, y;
    int width, height;
    Boolean unmap = XtIsRealized(w) && w->core.mapped_when_managed &&
		    XtIsManaged(w);

    if (unmap)
	XtUnmapWidget(w);

    if (!fw->form.resize_is_no_op)
	for (childP = children; childP - children < num_children; childP++) {
	    FormConstraints form = (FormConstraints)(*childP)->core.constraints;

	    if (!XtIsManaged(*childP))
		continue;

#ifndef OLDXAW
	    x = TransformCoord(form->form.virtual_x, fw->form.old_width,
			      XtWidth(fw), form->form.left);
	    y = TransformCoord(form->form.virtual_y, fw->form.old_height,
			       XtHeight(fw), form->form.top);
	    width = TransformCoord(form->form.virtual_x +
				   form->form.virtual_width +
				   (XtBorderWidth(*childP) << 1),
				   fw->form.old_width, XtWidth(fw),
				   form->form.right) -
				   (x + (XtBorderWidth(*childP) << 1));
	    height = TransformCoord(form->form.virtual_y +
				    form->form.virtual_height +
				    (XtBorderWidth(*childP) << 1),
				    fw->form.old_height, XtHeight(fw),
				    form->form.bottom) -
				    (y + (XtBorderWidth(*childP) << 1));
#else
	    x = TransformCoord(XtX(*childP), fw->form.old_width,
			      XtWidth(fw), form->form.left);
	    y = TransformCoord(XtY(*childP), fw->form.old_height,
			       XtHeight(fw), form->form.top);
	    width = TransformCoord(XtX(*childP) + form->form.virtual_width +
				   (XtBorderWidth(*childP) << 1),
				   fw->form.old_width, XtWidth(fw),
				   form->form.right) -
				   (x + (XtBorderWidth(*childP) << 1));
	    height = TransformCoord(XtY(*childP) + form->form.virtual_height +
				    (XtBorderWidth(*childP) << 1),
				    fw->form.old_height, XtHeight(fw),
				    form->form.bottom) -
				    (y + (XtBorderWidth(*childP) << 1));
	    form->form.virtual_width = width;
	    form->form.virtual_height = height;
#endif

	    width = width < 1 ? 1 : width;
	    height = height < 1 ? 1 : height;

	    XtConfigureWidget(*childP,
			      (Position)x, (Position)y,
			      (Dimension)width, (Dimension)height,
			      XtBorderWidth(*childP));
	}

    if (unmap)
	XtMapWidget(w);

#ifdef OLDXAW
    fw->form.old_width = XtWidth(fw);
    fw->form.old_height = XtHeight(fw);
#endif
}

/*ARGSUSED*/
static XtGeometryResult
XawFormGeometryManager(Widget w, XtWidgetGeometry *request,
		       XtWidgetGeometry *reply _X_UNUSED)
{
    Dimension old_width, old_height;
    FormWidget fw = (FormWidget)XtParent(w);
    FormConstraints form = (FormConstraints)w->core.constraints;
    XtWidgetGeometry allowed;
    XtGeometryResult ret_val;

    if ((request->request_mode & (unsigned)~(XtCWQueryOnly | CWWidth | CWHeight))
	|| !form->form.allow_resize) {
	/* If GeometryManager is invoked during a SetValues call on a child
	 * then it is necessary to compute a new layout if ConstraintSetValues
	 * allowed any constraint changes
	 */
	if (fw->form.needs_relayout)
	    (*((FormWidgetClass)fw->core.widget_class)->form_class.layout)
		(fw, 0, 0, True);
	return (XtGeometryNo);
    }

    if (request->request_mode & CWWidth)
	allowed.width = request->width;
    else
	allowed.width = XtWidth(w);

    if (request->request_mode & CWHeight)
	allowed.height = request->height;
    else
	allowed.height = XtHeight(w);

    if (allowed.width == XtWidth(w) && allowed.height == XtHeight(w)) {
	/* If GeometryManager is invoked during a SetValues call on a child
	 * then it is necessary to compute a new layout if ConstraintSetValues
	 * allowed any constraint changes
	 */
	if (fw->form.needs_relayout)
	    (*((FormWidgetClass)fw->core.widget_class)->form_class.layout)
		(fw, 0, 0, True);
	return (XtGeometryNo);
    }

    /*
     * Remember the old size, and then set the size to the requested size
     */
    old_width = XtWidth(w);
    old_height = XtHeight(w);
    XtWidth(w) = allowed.width;
    XtHeight(w) = allowed.height;

    if (request->request_mode & XtCWQueryOnly) {
	Boolean always_resize_children;
	Dimension ret_width, ret_height;

	fw->form.resize_in_layout = False;

	(*((FormWidgetClass)fw->core.widget_class)->form_class.layout)
	    (fw, XtWidth(w), XtHeight(w), False);

	/*
	 * Reset the size of this child back to what it used to be
	 */
	XtWidth(w) = old_width;
	XtHeight(w) = old_height;

	fw->form.resize_in_layout = True;

	always_resize_children = (Boolean)ChangeFormGeometry(w, True,
				   fw->form.preferred_width,
				   fw->form.preferred_height,
				   &ret_width, &ret_height);

	if (always_resize_children
	    || (ret_width >= fw->form.preferred_width
		&& ret_height >= fw->form.preferred_height))
	    ret_val = XtGeometryYes;
	else
	    ret_val = XtGeometryNo;
    }
    else {
	if ((*((FormWidgetClass)fw->core.widget_class)->form_class.layout)
		(fw, XtWidth(w), XtHeight(w), False)) {
	    Widget *childP;
	    int num_children = (int)fw->composite.num_children;
	    WidgetList children = fw->composite.children;

	    if (fw->form.no_refigure) {
		/*
		 * I am changing the widget wrapper w/o modifying the window.
		 * This is risky, but I can get away with it since I am the
		 * parent of this widget, and he must ask me for any geometry
		 * changes
		 *
		 * The window will be updated when no_refigure is set back
		 * to False
		 */
		form->form.deferred_resize = True;
		ret_val = XtGeometryDone;
	    }
	    else
		ret_val = XtGeometryYes;

	    /*
	     * Resets everything.
	     */
	    fw->form.old_width = XtWidth(fw);
	    fw->form.old_height = XtHeight(fw);
	    for (childP = children; childP - children < num_children; childP++) {
		Widget nw = *childP;

		if (XtIsManaged(nw)) {
		    FormConstraints nform = (FormConstraints)nw->core.constraints;

#ifndef OLDXAW
		    nform->form.virtual_x = XtX(nw);
		    nform->form.virtual_y = XtY(nw);
#endif
		    nform->form.virtual_width = (short)XtWidth(nw);
		    nform->form.virtual_height = (short)XtHeight(nw);
		}
	    }
	}
	else {
	    XtWidth(w) = old_width;
	    XtHeight(w) = old_height;
	    ret_val = XtGeometryNo;
	}
    }

    return (ret_val);
}

/*ARGSUSED*/
static Boolean
XawFormSetValues(Widget current _X_UNUSED, Widget request _X_UNUSED, Widget cnew _X_UNUSED,
		 ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
#ifndef OLDXAW
    FormWidget f_old = (FormWidget)current;
    FormWidget f_new = (FormWidget)cnew;

    if (f_old->core.background_pixmap != f_new->core.background_pixmap) {
	XawPixmap *opix, *npix;

	opix = XawPixmapFromXPixmap(f_old->core.background_pixmap, XtScreen(f_old),
				    f_old->core.colormap, (int)f_old->core.depth);
	npix = XawPixmapFromXPixmap(f_new->core.background_pixmap, XtScreen(f_new),
				    f_new->core.colormap, (int)f_new->core.depth);
	if ((npix && npix->mask) || (opix && opix->mask))
	    XawReshapeWidget(cnew, npix);
    }
#endif /* OLDXAW */

    return (False);
}

/* ARGSUSED */
static void
XawFormConstraintInitialize(Widget request _X_UNUSED, Widget cnew,
			    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    FormConstraints form = (FormConstraints)cnew->core.constraints;
    FormWidget fw = (FormWidget)cnew->core.parent;

#ifndef OLDXAW
    form->form.virtual_x = XtX(cnew);
    form->form.virtual_y = XtY(cnew);
#endif
    form->form.virtual_width = (short)XtWidth(cnew);
    form->form.virtual_height = (short)XtHeight(cnew);

    if (form->form.dx == default_value)
	form->form.dx = fw->form.default_spacing;

    if (form->form.dy == default_value)
	form->form.dy = fw->form.default_spacing;

    form->form.deferred_resize = False;
}

/*ARGSUSED*/
static Boolean
XawFormConstraintSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
			   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    FormConstraints cfc = (FormConstraints)current->core.constraints;
    FormConstraints nfc = (FormConstraints)cnew->core.constraints;

    if (cfc->form.top != nfc->form.top || cfc->form.bottom != nfc->form.bottom
	|| cfc->form.left != nfc->form.left || cfc->form.right != nfc->form.right
	|| cfc->form.dx != nfc->form.dx || cfc->form.dy != nfc->form.dy
	|| cfc->form.horiz_base != nfc->form.horiz_base
	|| cfc->form.vert_base != nfc->form.vert_base) {
	FormWidget fp = (FormWidget)XtParent(cnew);

	/* If there are no subclass ConstraintSetValues procedures remaining
	 * to be invoked, and if there is no geometry request about to be
	 * made, then invoke the new layout now; else defer it
	 */
	if (XtClass(XtParent(cnew)) == formWidgetClass
	    && XtX(current) == XtX(cnew)
	    && XtY(current) == XtY(cnew)
	    && XtWidth(current) == XtWidth(cnew)
	    && XtHeight(current) == XtHeight(cnew)
	    && XtBorderWidth(current) == XtBorderWidth(cnew))
	    Layout(fp, 0, 0, True);
	else
	    fp->form.needs_relayout = True;
    }

    return (False);
}

static void
XawFormChangeManaged(Widget w)
{
    FormWidget fw = (FormWidget)w;
    FormConstraints form;
    WidgetList children, childP;
    int num_children = (int)fw->composite.num_children;
    Widget child;

    (*((FormWidgetClass)w->core.widget_class)->form_class.layout)
	(fw, XtWidth(w), XtHeight(w), True);

    fw->form.old_width = XtWidth(w);
    fw->form.old_height = XtHeight(w);
    for (children = childP = fw->composite.children;
	 childP - children < num_children;
	 childP++) {
	child = *childP;
	if (!XtIsManaged(child))
	    continue;
	form = (FormConstraints)child->core.constraints;
#ifndef OLDXAW
	form->form.virtual_x = XtX(child);
	form->form.virtual_y = XtY(child);
#endif
	form->form.virtual_width = (short)XtWidth(child);
	form->form.virtual_height = (short)XtHeight(child);
    }
}

static XtGeometryResult
XawFormQueryGeometry(Widget widget, XtWidgetGeometry *request,
		     XtWidgetGeometry *reply)
{
    FormWidget w = (FormWidget)widget;

    reply->width = w->form.preferred_width;
    reply->height = w->form.preferred_height;
    reply->request_mode = CWWidth | CWHeight;

    if ((request->request_mode & (CWWidth | CWHeight)) == (CWWidth | CWHeight)
	&& request->width == reply->width
	&& request->height == reply->height)
	return (XtGeometryYes);
    else if (reply->width == XtWidth(w) && reply->height == XtHeight(w))
	return (XtGeometryNo);

    return (XtGeometryAlmost);
}

/*
 * Public routines
 */
/*
 * Set or reset figuring (ignored if not realized)
 */
void
XawFormDoLayout(Widget w,
#if NeedWidePrototypes
		Bool force
#else
		Boolean force
#endif
)
{
    Widget *childP;
    FormWidget fw = (FormWidget)w;
    int num_children = (int)fw->composite.num_children;
    WidgetList children = fw->composite.children;

    if ((fw->form.no_refigure = !force) == True || !XtIsRealized(w))
	return;

    for (childP = children; childP - children < num_children; childP++) {
	Widget nw = *childP;

	if (XtIsManaged(nw)) {
	    FormConstraints form = (FormConstraints)nw->core.constraints;

	    /*
	     * Xt Configure widget is too smart, and optimizes out
	     * my changes
	     */
	    XMoveResizeWindow(XtDisplay(nw), XtWindow(nw),
			      XtX(nw), XtY(nw), XtWidth(nw), XtHeight(nw));

	    if (form)
	      if (form->form.deferred_resize &&
		XtClass(nw)->core_class.resize != NULL) {
		(*(XtClass(nw)->core_class.resize))(nw);
		form->form.deferred_resize = False;
	      }
	}
    }
}
