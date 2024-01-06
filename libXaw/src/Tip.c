/*
 * Copyright (c) 1999 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from the
 * XFree86 Project.
 *
 * Author: Paulo César Pereira de Andrade
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xos.h>
#include <X11/Xaw/TipP.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xmu/Converters.h>
#include "Private.h"

#define	TIP_EVENT_MASK (ButtonPressMask	  |	\
			ButtonReleaseMask |	\
			PointerMotionMask |	\
			ButtonMotionMask  |	\
			KeyPressMask	  |	\
			KeyReleaseMask	  |	\
			EnterWindowMask	  |	\
			LeaveWindowMask)

/*
 * Types
 */
typedef struct _XawTipInfo {
    Screen *screen;
    TipWidget tip;
    Widget widget;
    Bool mapped;
    struct _XawTipInfo *next;
} XawTipInfo;

/*
 * Class Methods
 */
static void XawTipClassInitialize(void);
static void XawTipInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawTipDestroy(Widget);
static void XawTipExpose(Widget, XEvent*, Region);
static void XawTipRealize(Widget, Mask*, XSetWindowAttributes*);
static Boolean XawTipSetValues(Widget, Widget, Widget, ArgList, Cardinal*);

/*
 * Prototypes
 */
static void TipEventHandler(Widget, XtPointer, XEvent*, Boolean*);
static void TipShellEventHandler(Widget, XtPointer, XEvent*, Boolean*);
static XawTipInfo *CreateTipInfo(Widget);
static XawTipInfo *FindTipInfo(Widget);
static void ResetTip(XawTipInfo*, Bool);
static void TipTimeoutCallback(XtPointer, XtIntervalId*);
static void TipLayout(XawTipInfo*);
static void TipPosition(XawTipInfo*);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(TipRec, tip.field)
static XtResource resources[] = {
  {
    XtNforeground,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    offset(foreground),
    XtRString,
    (XtPointer)XtDefaultForeground,
  },
  {
    XtNfont,
    XtCFont,
    XtRFontStruct,
    sizeof(XFontStruct*),
    offset(font),
    XtRString,
    (XtPointer)XtDefaultFont
  },
  {
    XtNfontSet,
    XtCFontSet,
    XtRFontSet,
    sizeof(XFontSet),
    offset(fontset),
    XtRString,
    (XtPointer)XtDefaultFontSet
  },
  {
    XtNtopMargin,
    XtCVerticalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(top_margin),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNbottomMargin,
    XtCVerticalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(bottom_margin),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNleftMargin,
    XtCHorizontalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(left_margin),
    XtRImmediate,
    (XtPointer)6
  },
  {
    XtNrightMargin,
    XtCHorizontalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(right_margin),
    XtRImmediate,
    (XtPointer)6
  },
  {
    XtNbackingStore,
    XtCBackingStore,
    XtRBackingStore,
    sizeof(int),
    offset(backing_store),
    XtRImmediate,
    (XtPointer)(Always + WhenMapped + NotUseful)
  },
  {
    XtNtimeout,
    XtCTimeout,
    XtRInt,
    sizeof(int),
    offset(timeout),
    XtRImmediate,
    (XtPointer)500
  },
  {
    XawNdisplayList,
    XawCDisplayList,
    XawRDisplayList,
    sizeof(XawDisplayList*),
    offset(display_list),
    XtRImmediate,
    NULL
  },
};
#undef offset

TipClassRec tipClassRec = {
  /* core */
  {
    (WidgetClass)&widgetClassRec,	/* superclass */
    "Tip",				/* class_name */
    sizeof(TipRec),			/* widget_size */
    XawTipClassInitialize,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawTipInitialize,			/* initialize */
    NULL,				/* initialize_hook */
    XawTipRealize,			/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawTipDestroy,			/* destroy */
    NULL,				/* resize */
    XawTipExpose,			/* expose */
    XawTipSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XtInheritQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* tip */
  {
    NULL,				/* extension */
  },
};

WidgetClass tipWidgetClass = (WidgetClass)&tipClassRec;

static XawTipInfo *first_tip;

/*
 * Implementation
 */
static void
XawTipClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtAddConverter(XtRString, XtRBackingStore, XmuCvtStringToBackingStore,
		   NULL, 0);
    XtSetTypeConverter(XtRBackingStore, XtRString, XmuCvtBackingStoreToString,
		       NULL, 0, XtCacheNone, NULL);
}

/*ARGSUSED*/
static void
XawTipInitialize(Widget req _X_UNUSED, Widget w _X_UNUSED, ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TipWidget tip = (TipWidget)w;
    XGCValues values;

    if (!tip->tip.font) XtError("Aborting: no font found\n");
    if (tip->tip.international && !tip->tip.fontset)
	XtError("Aborting: no fontset found\n");

    tip->tip.timer = 0;

    values.foreground = tip->tip.foreground;
    values.background = tip->core.background_pixel;
    values.font = tip->tip.font->fid;
    values.graphics_exposures = False;

    tip->tip.gc = XtAllocateGC(w, 0, GCForeground | GCBackground | GCFont |
			       GCGraphicsExposures, &values, GCFont, 0);
}

static void
XawTipDestroy(Widget w)
{
    XawTipInfo *info = FindTipInfo(w);
    TipWidget tip = (TipWidget)w;

    if (tip->tip.timer)
	XtRemoveTimeOut(tip->tip.timer);

    XtReleaseGC(w, tip->tip.gc);

    XtRemoveEventHandler(XtParent(w), KeyPressMask, False, TipShellEventHandler,
			 (XtPointer)NULL);
    if (info == first_tip)
	first_tip = first_tip->next;
    else {
	XawTipInfo *p = first_tip;

	while (p && p->next != info)
	    p = p->next;
	if (p)
	    p->next = info->next;
    }
    XtFree((char*)info);
}

static void
XawTipRealize(Widget w, Mask *mask, XSetWindowAttributes *attr)
{
    TipWidget tip = (TipWidget)w;

    if (tip->tip.backing_store == Always ||
	tip->tip.backing_store == NotUseful ||
	tip->tip.backing_store == WhenMapped) {
	*mask |= CWBackingStore;
	attr->backing_store = tip->tip.backing_store;
    }
    else
	*mask &= (Mask)(~CWBackingStore);
    *mask |= CWOverrideRedirect;
    attr->override_redirect = True;

    XtWindow(w) = XCreateWindow(DisplayOfScreen(XtScreen(w)),
				RootWindowOfScreen(XtScreen(w)),
				XtX(w), XtY(w),
				XtWidth(w) ? XtWidth(w) : 1,
				XtHeight(w) ? XtHeight(w) : 1,
				XtBorderWidth(w),
				DefaultDepthOfScreen(XtScreen(w)),
				InputOutput,
				(Visual *)CopyFromParent,
				*mask, attr);
}

static void
XawTipExpose(Widget w, XEvent *event, Region region)
{
    TipWidget tip = (TipWidget)w;
    GC gc = tip->tip.gc;
    char *nl;
    _Xconst char * label = tip->tip.label;
    Position y = (Position)(tip->tip.top_margin + tip->tip.font->max_bounds.ascent);
    int len;

    if (tip->tip.display_list)
	XawRunDisplayList(w, tip->tip.display_list, event, region);

    if (tip->tip.international == True) {
	Position ksy = (Position)tip->tip.top_margin;
	XFontSetExtents *ext = XExtentsOfFontSet(tip->tip.fontset);

	ksy = (ksy + XawAbs(ext->max_ink_extent.y));

	while ((nl = strchr(label, '\n')) != NULL) {
	    XmbDrawString(XtDisplay(w), XtWindow(w), tip->tip.fontset,
			  gc, tip->tip.left_margin, ksy, label,
			  (int)(nl - label));
	    ksy = (ksy + ext->max_ink_extent.height);
	    label = nl + 1;
	}
	len = (int)strlen(label);
	if (len)
	    XmbDrawString(XtDisplay(w), XtWindow(w), tip->tip.fontset, gc,
			  tip->tip.left_margin, ksy, label, len);
    }
    else {
	while ((nl = strchr(label, '\n')) != NULL) {
	    if (tip->tip.encoding)
		XDrawString16(XtDisplay(w), XtWindow(w), gc,
			      tip->tip.left_margin, y,
			      (_Xconst XChar2b*)label, (int)(nl - label) >> 1);
	    else
		XDrawString(XtDisplay(w), XtWindow(w), gc,
			    tip->tip.left_margin, y, label, (int)(nl - label));
	    y = (Position)(y + (tip->tip.font->max_bounds.ascent +
		  tip->tip.font->max_bounds.descent));
	    label = nl + 1;
	}
	len = (int)strlen(label);
	if (len) {
	    if (tip->tip.encoding)
		XDrawString16(XtDisplay(w), XtWindow(w), gc,
			      tip->tip.left_margin, y, (_Xconst XChar2b*)label, len >> 1);
	    else
		XDrawString(XtDisplay(w), XtWindow(w), gc,
			    tip->tip.left_margin, y, label, len);
	}
    }
}

/*ARGSUSED*/
static Boolean
XawTipSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TipWidget curtip = (TipWidget)current;
    TipWidget newtip = (TipWidget)cnew;
    Boolean redisplay = False;

    if (curtip->tip.font->fid != newtip->tip.font->fid ||
	curtip->tip.foreground != newtip->tip.foreground) {
	XGCValues values;

	values.foreground = newtip->tip.foreground;
	values.background = newtip->core.background_pixel;
	values.font = newtip->tip.font->fid;
	values.graphics_exposures = False;
	XtReleaseGC(cnew, curtip->tip.gc);
	newtip->tip.gc = XtAllocateGC(cnew, 0, GCForeground | GCBackground |
				      GCFont | GCGraphicsExposures, &values,
				      GCFont, 0);
	redisplay = True;
    }
    if (curtip->tip.display_list != newtip->tip.display_list)
	redisplay = True;

    return (redisplay);
}

static void
TipLayout(XawTipInfo *info)
{
    XFontStruct	*fs = info->tip->tip.font;
    int width = 0, height;
    char *nl;
    _Xconst char *label = info->tip->tip.label;

    if (info->tip->tip.international == True) {
	XFontSet fset = info->tip->tip.fontset;
	XFontSetExtents *ext = XExtentsOfFontSet(fset);

	height = ext->max_ink_extent.height;
	if ((nl = strchr(label, '\n')) != NULL) {
	    /*CONSTCOND*/
	    while (True) {
		int w = XmbTextEscapement(fset, label, (int)(nl - label));

		if (w > width)
		    width = w;
		if (*nl == '\0')
		    break;
		label = nl + 1;
		if (*label)
		    height += ext->max_ink_extent.height;
		if ((nl = strchr(label, '\n')) == NULL)
		    nl = strchr(label, '\0');
	    }
	}
	else
	    width = XmbTextEscapement(fset, label, (int)strlen(label));
    }
    else {
	height = fs->max_bounds.ascent + fs->max_bounds.descent;
	if ((nl = strchr(label, '\n')) != NULL) {
	    /*CONSTCOND*/
	    while (True) {
		int w = info->tip->tip.encoding ?
		    XTextWidth16(fs, (_Xconst XChar2b*)label, (int)(nl - label) >> 1) :
		    XTextWidth(fs, label, (int)(nl - label));
		if (w > width)
		    width = w;
		if (*nl == '\0')
		    break;
		label = nl + 1;
		if (*label)
		    height += fs->max_bounds.ascent + fs->max_bounds.descent;
		if ((nl = strchr(label, '\n')) == NULL)
		    nl = strchr(label, '\0');
	    }
	}
	else
	    width = info->tip->tip.encoding ?
		XTextWidth16(fs, (_Xconst XChar2b*)label, (int)(strlen(label) >> 1)) :
		XTextWidth(fs, label, (int)strlen(label));
    }
    XtWidth(info->tip) = (width + info->tip->tip.left_margin +
				  info->tip->tip.right_margin);
    XtHeight(info->tip) = (height + info->tip->tip.top_margin +
				    info->tip->tip.bottom_margin);
}

#define	DEFAULT_TIP_Y_OFFSET	12
static void
TipPosition(XawTipInfo *info)
{
    Window r, c;
    int rx, ry, wx, wy;
    unsigned mask;
    Position x, y;

    XQueryPointer(XtDisplay((Widget)info->tip), XtWindow((Widget)info->tip),
		  &r, &c, &rx, &ry, &wx, &wy, &mask);
    x = (Position)(rx - (XtWidth(info->tip) >> 1));
    y = (Position)(ry + DEFAULT_TIP_Y_OFFSET);

    if (x >= 0) {
	int scr_width = WidthOfScreen(XtScreen(info->tip));

	if (x + XtWidth(info->tip) + XtBorderWidth(info->tip) > scr_width)
	    x = (Position)(scr_width - XtWidth(info->tip) - XtBorderWidth(info->tip));
    }
    if (x < 0)
	x = 0;
    if (y >= 0) {
	int scr_height = HeightOfScreen(XtScreen(info->tip));

	if (y + XtHeight(info->tip) + XtBorderWidth(info->tip) > scr_height)
	    y = (Position)(y - (XtHeight(info->tip) + XtBorderWidth(info->tip) +
		 (DEFAULT_TIP_Y_OFFSET << 1)));
    }
    if (y < 0)
	y = 0;

    XMoveResizeWindow(XtDisplay(info->tip), XtWindow(info->tip),
		      (int)(XtX(info->tip) = x), (int)(XtY(info->tip) = y),
		      (unsigned)XtWidth(info->tip), (unsigned)XtHeight(info->tip));
}

static XawTipInfo *
CreateTipInfo(Widget w)
{
    XawTipInfo *info = XtNew(XawTipInfo);
    Widget shell = w;

    info->screen = XtScreen(w);

    while (XtParent(shell))
	shell = XtParent(shell);

    info->tip = (TipWidget)XtCreateWidget("tip", tipWidgetClass, shell, NULL, 0);
    XtRealizeWidget((Widget)info->tip);
    info->widget = NULL;
    info->mapped = False;
    info->next = NULL;
    XtAddEventHandler(shell, KeyPressMask, False, TipShellEventHandler,
		      (XtPointer)NULL);

    return (info);
}

static XawTipInfo *
FindTipInfo(Widget w)
{
    XawTipInfo *ptip, *tip = first_tip;
    Screen *screen = XtScreenOfObject(w);

    if (tip == NULL)
	return (first_tip = tip = CreateTipInfo(w));

    for (ptip = tip; tip; ptip = tip, tip = tip->next)
	if (tip->screen == screen)
	    return (tip);

    return (ptip->next = CreateTipInfo(w));
}

static void
ResetTip(XawTipInfo *info, Bool add_timeout)
{
    if (info->tip->tip.timer) {
	XtRemoveTimeOut(info->tip->tip.timer);
	info->tip->tip.timer = 0;
    }
    if (info->mapped) {
	XtRemoveGrab(XtParent((Widget)info->tip));
	XUnmapWindow(XtDisplay((Widget)info->tip), XtWindow((Widget)info->tip));
	info->mapped = False;
    }
    if (add_timeout) {
	info->tip->tip.timer =
	    XtAppAddTimeOut(XtWidgetToApplicationContext((Widget)info->tip),
			    (unsigned long)info->tip->tip.timeout,
			    TipTimeoutCallback,
			    (XtPointer)info);
    }
}

static void
TipTimeoutCallback(XtPointer closure, XtIntervalId *id _X_UNUSED)
{
    XawTipInfo *info = (XawTipInfo*)closure;
    Arg args[3];

    info->tip->tip.label = NULL;
    info->tip->tip.international = False;
    info->tip->tip.encoding = 0;
    info->tip->tip.timer = 0;
    XtSetArg(args[0], XtNtip, &info->tip->tip.label);
    XtSetArg(args[1], XtNinternational, &info->tip->tip.international);
    XtSetArg(args[2], XtNencoding, &info->tip->tip.encoding);
    XtGetValues(info->widget, args, 3);

    if (info->tip->tip.label) {
	TipLayout(info);
	TipPosition(info);
	XMapRaised(XtDisplay((Widget)info->tip), XtWindow((Widget)info->tip));
	XtAddGrab(XtParent((Widget)info->tip), True, True);
	info->mapped = True;
    }
}

/*ARGSUSED*/
static void
TipShellEventHandler(Widget w, XtPointer client_data _X_UNUSED, XEvent *event _X_UNUSED,
		     Boolean *continue_to_dispatch _X_UNUSED)
{
    ResetTip(FindTipInfo(w), False);
}

/*ARGSUSED*/
static void
TipEventHandler(Widget w, XtPointer client_data _X_UNUSED, XEvent *event,
		Boolean *continue_to_dispatch _X_UNUSED)
{
    XawTipInfo *info = FindTipInfo(w);
    Boolean add_timeout;

    if (info->widget != w) {
	ResetTip(info, False);
	info->widget = w;
    }

    switch (event->type) {
	case EnterNotify:
	    add_timeout = True;
	    break;
	case MotionNotify:
	    /* If any button is pressed, timer is 0 */
	    if (info->mapped)
		return;
	    add_timeout = info->tip->tip.timer != 0;
	    break;
	default:
	    add_timeout = False;
	    break;
    }
    ResetTip(info, add_timeout);
}

/*
 * Public routines
 */
void
XawTipEnable(Widget w)
{
    XtAddEventHandler(w, TIP_EVENT_MASK, False, TipEventHandler,
		      (XtPointer)NULL);
}

void
XawTipDisable(Widget w)
{
    XawTipInfo *info = FindTipInfo(w);

    XtRemoveEventHandler(w, TIP_EVENT_MASK, False, TipEventHandler,
			 (XtPointer)NULL);
    if (info->widget == w)
	ResetTip(info, False);
}
