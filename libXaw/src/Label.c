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
#include <ctype.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xos.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xaw/LabelP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#define streq(a, b)		(strcmp((a), (b)) == 0)

#define MULTI_LINE_LABEL 32767


/*
 * Class Methods
 */
static void XawLabelClassInitialize(void);
static void XawLabelDestroy(Widget);
static void XawLabelInitialize(Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult XawLabelQueryGeometry(Widget, XtWidgetGeometry*,
					      XtWidgetGeometry*);
static void XawLabelRedisplay(Widget, XEvent*, Region);
static void XawLabelResize(Widget);
static Boolean XawLabelSetValues(Widget, Widget, Widget,
				 ArgList, Cardinal*);

/*
 * Prototypes
 */
static void compute_bitmap_offsets(LabelWidget);
static void GetGrayGC(LabelWidget);
static void GetNormalGC(LabelWidget);
static void _Reposition(LabelWidget, unsigned int, unsigned int,
			Position*, Position*);
static void set_bitmap_info(LabelWidget);
static void SetTextWidthAndHeight(LabelWidget);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(LabelRec, field)
static XtResource resources[] = {
  {
    XtNforeground,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    offset(label.foreground),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNfont,
    XtCFont,
    XtRFontStruct,
    sizeof(XFontStruct*),
    offset(label.font),
    XtRString,
    (XtPointer)XtDefaultFont
  },
  {
    XtNfontSet,
    XtCFontSet,
    XtRFontSet,
    sizeof(XFontSet),
    offset(label.fontset),
    XtRString,
    (XtPointer)XtDefaultFontSet
  },
  {
    XtNlabel,
    XtCLabel,
    XtRString,
    sizeof(String),
    offset(label.label),
    XtRString,
    NULL
  },
  {
    XtNencoding,
    XtCEncoding,
    XtRUnsignedChar,
    sizeof(unsigned char),
    offset(label.encoding),
    XtRImmediate,
    (XtPointer)XawTextEncoding8bit
  },
  {
    XtNjustify,
    XtCJustify,
    XtRJustify,
    sizeof(XtJustify),
    offset(label.justify),
    XtRImmediate,
    (XtPointer)XtJustifyCenter
  },
  {
    XtNinternalWidth,
    XtCWidth,
    XtRDimension,
    sizeof(Dimension),
    offset(label.internal_width),
    XtRImmediate,
    (XtPointer)4
  },
  {
    XtNinternalHeight,
    XtCHeight,
    XtRDimension,
    sizeof(Dimension),
    offset(label.internal_height),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNleftBitmap,
    XtCLeftBitmap,
    XtRBitmap,
    sizeof(Pixmap),
    offset(label.left_bitmap),
    XtRImmediate,
    (XtPointer)None
  },
  {
    XtNbitmap,
    XtCPixmap,
    XtRBitmap,
    sizeof(Pixmap),
    offset(label.pixmap),
    XtRImmediate,
    (XtPointer)None
  },
  {
    XtNresize,
    XtCResize,
    XtRBoolean,
    sizeof(Boolean),
    offset(label.resize),
    XtRImmediate,
    (XtPointer)True
  },
  {
    XtNlabelX,
    XtCPosition,
    XtRPosition,
    sizeof(Position),
    offset(label.label_x),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNlabelY,
    XtCPosition,
    XtRPosition,
    sizeof(Position),
    offset(label.label_y),
    XtRImmediate,
    (XtPointer)0
  },
};
#undef offset

#define Superclass (&simpleClassRec)
LabelClassRec labelClassRec = {
  /* core */
  {
    (WidgetClass)&simpleClassRec,	/* superclass */
    "Label",				/* class_name */
    sizeof(LabelRec),			/* widget_size */
    XawLabelClassInitialize,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawLabelInitialize,			/* initialize */
    NULL,				/* initialize_hook */
    XtInheritRealize,			/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawLabelDestroy,			/* destroy */
    XawLabelResize,			/* resize */
    XawLabelRedisplay,			/* expose */
    XawLabelSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XawLabelQueryGeometry,		/* query_geometry */
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
  }
};

WidgetClass labelWidgetClass = (WidgetClass)&labelClassRec;

/*
 * Implementation
 */
static void
XawLabelClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtAddConverter(XtRString, XtRJustify, XmuCvtStringToJustify, NULL, 0);
    XtSetTypeConverter(XtRJustify, XtRString, XmuCvtJustifyToString,
		       NULL, 0, XtCacheNone, NULL);
}

/*
 * Calculate width and height of displayed text in pixels
 */
static void
SetTextWidthAndHeight(LabelWidget lw)
{
    XFontStruct	*fs = lw->label.font;
    char *nl;

    if (lw->label.pixmap != None) {
	Window root;
	int x, y;
	unsigned int width, height, bw, depth;

	if (XGetGeometry(XtDisplay(lw), lw->label.pixmap, &root, &x, &y,
		       &width, &height, &bw, &depth)) {
	    lw->label.label_height = (Dimension)height;
	    lw->label.label_width = (Dimension)width;
	    lw->label.label_len = (Dimension)depth;
	    return;
	}
    }
    if (lw->simple.international == True) {
	XFontSet	fset = lw->label.fontset;
	XFontSetExtents *ext = XExtentsOfFontSet(fset);

	lw->label.label_height = ext->max_ink_extent.height;
	if (lw->label.label == NULL) {
	    lw->label.label_len = 0;
	    lw->label.label_width = 0;
	}
	else if ((nl = strchr(lw->label.label, '\n')) != NULL) {
	    char *label;

	    lw->label.label_len = MULTI_LINE_LABEL;
	    lw->label.label_width = 0;
	    for (label = lw->label.label; nl != NULL; nl = strchr(label, '\n')) {
		int width = XmbTextEscapement(fset, label, (int)(nl - label));

		if (width > (int)lw->label.label_width)
		    lw->label.label_width = (Dimension)width;
		label = nl + 1;
		if (*label)
		    lw->label.label_height = (Dimension)(lw->label.label_height + ext->max_ink_extent.height);
	    }
	    if (*label) {
		int width = XmbTextEscapement(fset, label, (int)strlen(label));

		if (width > (int)lw->label.label_width)
		    lw->label.label_width = (Dimension)width;
	    }
	}
	else {
	    lw->label.label_len = (Dimension)strlen(lw->label.label);
	    lw->label.label_width =
		(Dimension)XmbTextEscapement(fset, lw->label.label, lw->label.label_len);
	}
    }
    else {
	lw->label.label_height = (Dimension)(fs->max_bounds.ascent + fs->max_bounds.descent);
	if (lw->label.label == NULL) {
	    lw->label.label_len = 0;
	    lw->label.label_width = 0;
	}
	else if ((nl = strchr(lw->label.label, '\n')) != NULL) {
	    char *label;

	    lw->label.label_len = MULTI_LINE_LABEL;
	    lw->label.label_width = 0;
	    for (label = lw->label.label; nl != NULL; nl = strchr(label, '\n')) {
		int width;

		if (lw->label.encoding)
		    width = XTextWidth16(fs, (XChar2b *)label, (int)(nl - label) / 2);
		else
		    width = XTextWidth(fs, label, (int)(nl - label));
		if (width > (int)lw->label.label_width)
		    lw->label.label_width = (Dimension)width;
		label = nl + 1;
		if (*label)
		    lw->label.label_height +=
			fs->max_bounds.ascent + fs->max_bounds.descent;
	    }
	    if (*label) {
		int width = XTextWidth(fs, label, (int)strlen(label));

		if (lw->label.encoding)
		    width = XTextWidth16(fs, (XChar2b *)label, (int)(strlen(label) / 2));
		else
		    width = XTextWidth(fs, label, (int)strlen(label));
		if (width > (int) lw->label.label_width)
		    lw->label.label_width = (Dimension)width;
	    }
	}
	else {
	    lw->label.label_len = (Dimension)strlen(lw->label.label);
	    if (lw->label.encoding)
		lw->label.label_width =
		    (Dimension)XTextWidth16(fs, (XChar2b *)lw->label.label,
			   (int)lw->label.label_len / 2);
	    else
		lw->label.label_width =
		    (Dimension)XTextWidth(fs, lw->label.label, (int)lw->label.label_len);
	}
    }
}

static void
GetNormalGC(LabelWidget lw)
{
    XGCValues	values;

    values.foreground	= lw->label.foreground;
    values.background	= lw->core.background_pixel;
    values.font		= lw->label.font->fid;
    values.graphics_exposures = False;

    if (lw->simple.international == True)
    /* Since Xmb/wcDrawString eats the font, I must use XtAllocateGC */
	lw->label.normal_GC = XtAllocateGC((Widget)lw, 0,
					   GCForeground | GCBackground |
					   GCGraphicsExposures,
					   &values, GCFont, 0);
    else
	lw->label.normal_GC = XtGetGC((Widget)lw,
				      GCForeground | GCBackground | GCFont |
				      GCGraphicsExposures, &values);
}

static void
GetGrayGC(LabelWidget lw)
{
    XGCValues	values;

    values.foreground = lw->label.foreground;
    values.background = lw->core.background_pixel;
    values.font	      = lw->label.font->fid;
    values.fill_style = FillTiled;
    values.tile       = XmuCreateStippledPixmap(XtScreen((Widget)lw),
						lw->label.foreground,
						lw->core.background_pixel,
						lw->core.depth);
    values.graphics_exposures = False;

    lw->label.stipple = values.tile;
    if (lw->simple.international == True)
	/* Since Xmb/wcDrawString eats the font, I must use XtAllocateGC */
	lw->label.gray_GC = XtAllocateGC((Widget)lw, 0,
					 GCForeground | GCBackground |
					 GCTile | GCFillStyle |
					 GCGraphicsExposures,
					 &values, GCFont, 0);
	else
	    lw->label.gray_GC = XtGetGC((Widget)lw,
					GCForeground | GCBackground |
					GCFont | GCTile | GCFillStyle |
					GCGraphicsExposures,
					&values);
}

static void
compute_bitmap_offsets(LabelWidget lw)
{
    /*
     * bitmap will be eventually be displayed at
     * (internal_width, internal_height + lbm_y)
     */
    if (lw->label.lbm_height != 0)
	lw->label.lbm_y = (int)((XtHeight(lw)
				- ((lw->label.internal_height * 2)
				+ lw->label.lbm_height)) / 2);
    else
	lw->label.lbm_y = 0;
}

static void
set_bitmap_info(LabelWidget lw)
{
    Window root;
    int x, y;
    unsigned int bw, depth;

    if (!(lw->label.left_bitmap
	&& XGetGeometry(XtDisplay(lw), lw->label.left_bitmap, &root, &x, &y,
			&lw->label.lbm_width, &lw->label.lbm_height,
			&bw, &depth)))
	lw->label.lbm_width = lw->label.lbm_height = 0;

    compute_bitmap_offsets(lw);
}

/*ARGSUSED*/
static void
XawLabelInitialize(Widget request _X_UNUSED, Widget cnew,
		   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    LabelWidget lw = (LabelWidget)cnew;

    if (!lw->label.font) XtError("Aborting: no font found\n");
    if (lw->simple.international && !lw->label.fontset)
	XtError("Aborting: no fontset found\n");

    if (lw->label.label == NULL)
	lw->label.label = XtNewString(lw->core.name);
    else
	lw->label.label = XtNewString(lw->label.label);

    GetNormalGC(lw);
    GetGrayGC(lw);

    SetTextWidthAndHeight(lw);

    if (XtHeight(lw) == 0)
	XtHeight(lw) = (Dimension)(lw->label.label_height + 2 * lw->label.internal_height);

    set_bitmap_info(lw);		/* need core.height */

    if (XtWidth(lw) == 0)		/* need label.lbm_width */
	XtWidth(lw) = (lw->label.label_width
		       + (2 * lw->label.internal_width)
		       + LEFT_OFFSET(lw));

    lw->label.label_x = lw->label.label_y = 0;
    (*XtClass(cnew)->core_class.resize)((Widget)lw);
}

/*ARGSUSED*/
static void
XawLabelRedisplay(Widget gw, XEvent *event, Region region)
{
    LabelWidget w = (LabelWidget)gw;
    GC gc;

    if (*Superclass->core_class.expose != NULL)
	(*Superclass->core_class.expose)(gw, event, region);

    gc = XtIsSensitive(gw) ? w->label.normal_GC : w->label.gray_GC;
#ifdef notdef
    if (region != NULL)
	XSetRegion(XtDisplay(gw), gc, region);
#endif /*notdef*/

    if (w->label.pixmap == None) {
	int len = w->label.label_len;
	char *label = w->label.label;
	Position y = (Position)(w->label.label_y + w->label.font->max_bounds.ascent);
	Position ksy = w->label.label_y;

	/* display left bitmap */
	if (w->label.left_bitmap && w->label.lbm_width != 0)
	    XCopyPlane (XtDisplay(gw), w->label.left_bitmap, XtWindow(gw), gc,
			0, 0, w->label.lbm_width, w->label.lbm_height,
			w->label.internal_width,
			w->label.internal_height + w->label.lbm_y, 1L);

	if (w->simple.international == True) {
	    XFontSetExtents *ext = XExtentsOfFontSet(w->label.fontset);

	    ksy = (ksy + XawAbs(ext->max_ink_extent.y));

	    if (len == MULTI_LINE_LABEL) {
		char *nl;

		while ((nl = strchr(label, '\n')) != NULL) {
		    XmbDrawString(XtDisplay(w), XtWindow(w), w->label.fontset,
				  gc, w->label.label_x, ksy, label,
				  (int)(nl - label));
		    ksy = (ksy + ext->max_ink_extent.height);
		    label = nl + 1;
		}
		len = (int)strlen(label);
	    }
	    if (len)
		XmbDrawString(XtDisplay(w), XtWindow(w), w->label.fontset, gc,
			      w->label.label_x, ksy, label, len);
	}
	else {
	    if (len == MULTI_LINE_LABEL) {
		char *nl;

		while ((nl = strchr(label, '\n')) != NULL) {
		    if (w->label.encoding)
			XDrawString16(XtDisplay(gw), XtWindow(gw), gc,
				      w->label.label_x, y,
				      (XChar2b *)label, (int)(nl - label) / 2);
		    else
			XDrawString(XtDisplay(gw), XtWindow(gw), gc,
				    w->label.label_x, y, label, (int)(nl - label));
		    y += (w->label.font->max_bounds.ascent +
			 w->label.font->max_bounds.descent);
		    label = nl + 1;
		}
		len = (int)strlen(label);
	    }
	    if (len) {
		if (w->label.encoding)
		    XDrawString16(XtDisplay(gw), XtWindow(gw), gc,
				  w->label.label_x, y, (XChar2b *)label, len / 2);
		else
		    XDrawString(XtDisplay(gw), XtWindow(gw), gc,
				w->label.label_x, y, label, len);
	    }
	}
    }
    else if (w->label.label_len == 1)
	XCopyPlane(XtDisplay(gw), w->label.pixmap, XtWindow(gw), gc,
		   0, 0, w->label.label_width, w->label.label_height,
		   w->label.label_x, w->label.label_y, 1L);
    else
	XCopyArea(XtDisplay(gw), w->label.pixmap, XtWindow(gw), gc,
		  0, 0, w->label.label_width, w->label.label_height,
		  w->label.label_x, w->label.label_y);

#ifdef notdef
    if (region != NULL)
	XSetClipMask(XtDisplay(gw), gc, (Pixmap)None);
#endif /* notdef */
}

static void
_Reposition(LabelWidget lw, unsigned int width, unsigned int height,
	    Position *dx, Position *dy)
{
    Position newPos;
    Position leftedge = (lw->label.internal_width + LEFT_OFFSET(lw));

    switch (lw->label.justify) {
	case XtJustifyLeft:
	    newPos = leftedge;
	    break;
	case XtJustifyRight:
	    newPos = (Position)(width - (unsigned)(lw->label.label_width + lw->label.internal_width));
	    break;
	case XtJustifyCenter:
        /*FALLTRHOUGH*/
	default:
	    newPos = (Position)((int)(width - lw->label.label_width) >> 1);
	    break;
    }
    if (newPos < (Position)leftedge)
	newPos = leftedge;
    *dx = (Position)(newPos - lw->label.label_x);
    lw->label.label_x = newPos;

    newPos = (Position)((height - lw->label.label_height) >> 1);
    *dy = (Position)(newPos - lw->label.label_y);
    lw->label.label_y = newPos;
}

static void
XawLabelResize(Widget w)
{
    LabelWidget lw = (LabelWidget)w;
    Position dx, dy;

    _Reposition(lw, XtWidth(w), XtHeight(w), &dx, &dy);
    compute_bitmap_offsets(lw);
}

#define PIXMAP 0
#define WIDTH 1
#define HEIGHT 2
#define NUM_CHECKS 3
static Boolean
XawLabelSetValues(Widget current, Widget request, Widget cnew,
		  ArgList args, Cardinal *num_args)
{
    LabelWidget curlw = (LabelWidget)current;
    LabelWidget reqlw = (LabelWidget)request;
    LabelWidget newlw = (LabelWidget)cnew;
    unsigned int i;
    Boolean was_resized = False, redisplay = False, checks[NUM_CHECKS];

    for (i = 0; i < NUM_CHECKS; i++)
	checks[i] = False;

    for (i = 0; i < *num_args; i++) {
	if (streq(XtNbitmap, args[i].name))
	    checks[PIXMAP] = True;
	else if (streq(XtNwidth, args[i].name))
	    checks[WIDTH] = True;
	else if (streq(XtNheight, args[i].name))
	    checks[HEIGHT] = True;
    }

    if (newlw->label.label == NULL)
	newlw->label.label = (char *)newlw->core.name;

    /*
     * resize on bitmap change
     */
    if (curlw->label.left_bitmap != newlw->label.left_bitmap)
	was_resized = True;

    if (curlw->label.encoding != newlw->label.encoding)
	was_resized = True;

    if (curlw->simple.international
	&& curlw->label.fontset != newlw->label.fontset)
	was_resized = True;

    if (curlw->label.label != newlw->label.label) {
	if (curlw->label.label != curlw->core.name)
	    XtFree((char *)curlw->label.label);

	if (newlw->label.label != newlw->core.name)
	    newlw->label.label = XtNewString(newlw->label.label);

	was_resized = True;
    }

    if (was_resized || (curlw->label.font != newlw->label.font) ||
        curlw->label.justify != newlw->label.justify || checks[PIXMAP]) {
	SetTextWidthAndHeight(newlw);
	was_resized = True;
    }

    /* recalculate the window size if something has changed */
    if (newlw->label.resize && was_resized) {
	if (XtHeight(curlw) == XtHeight(reqlw) && !checks[HEIGHT])
	    XtHeight(newlw) = (Dimension)(newlw->label.label_height +
			      (newlw->label.internal_height << 1));

	set_bitmap_info(newlw);

	if (XtWidth(curlw) == XtWidth(reqlw) && !checks[WIDTH])
	    XtWidth(newlw) = (newlw->label.label_width
			      + LEFT_OFFSET(newlw)
			      + (unsigned)(newlw->label.internal_width << 1));
    }

    if (curlw->label.foreground		!= newlw->label.foreground
	|| curlw->core.background_pixel != newlw->core.background_pixel
	|| curlw->label.font->fid	!= newlw->label.font->fid) {
	/* The Fontset is not in the GC - don't make a new GC if FS changes! */
	XtReleaseGC(cnew, curlw->label.normal_GC);
	XtReleaseGC(cnew, curlw->label.gray_GC);
	XmuReleaseStippledPixmap(XtScreen(current), curlw->label.stipple);
	GetNormalGC(newlw);
	GetGrayGC(newlw);
	redisplay = True;
    }

    if (curlw->label.label_x != newlw->label.label_x ||
        curlw->label.label_y != newlw->label.label_y)
	redisplay = True;

    if (curlw->label.internal_width != newlw->label.internal_width
	|| curlw->label.internal_height != newlw->label.internal_height
	|| was_resized) {
	/* Resize() will be called if geometry changes succeed */
	Position dx, dy;

	_Reposition(newlw, XtWidth(curlw), XtHeight(curlw), &dx, &dy);
    }

      return (was_resized || redisplay ||
	      XtIsSensitive(current) != XtIsSensitive(cnew));
}

static void
XawLabelDestroy(Widget w)
{
    LabelWidget lw = (LabelWidget)w;

    if (lw->label.label != lw->core.name)
	XtFree(lw->label.label);
    XtReleaseGC(w, lw->label.normal_GC);
    XtReleaseGC(w, lw->label.gray_GC);
    XmuReleaseStippledPixmap(XtScreen(w), lw->label.stipple);
}

static XtGeometryResult
XawLabelQueryGeometry(Widget w, XtWidgetGeometry *intended,
		      XtWidgetGeometry *preferred)
{
    LabelWidget lw = (LabelWidget)w;

    preferred->request_mode = CWWidth | CWHeight;
    preferred->width = (lw->label.label_width
                        + (unsigned)(lw->label.internal_width << 1)
                        + LEFT_OFFSET(lw));
    preferred->height = (Dimension)(lw->label.label_height +
			(lw->label.internal_height << 1));

    if (((intended->request_mode & (CWWidth | CWHeight)) == (CWWidth | CWHeight))
	&& intended->width == preferred->width
	&& intended->height == preferred->height)
	return (XtGeometryYes);
      else if (preferred->width == XtWidth(w) && preferred->height == XtHeight(w))
	return (XtGeometryNo);

    return (XtGeometryAlmost);
}
