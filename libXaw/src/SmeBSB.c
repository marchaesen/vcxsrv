/*
Copyright 1989, 1994, 1998  The Open Group

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
 */

/*
 * SmeBSB.c - Source code file for BSB Menu Entry object.
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
#include <X11/Xos.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xaw/Cardinals.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/SmeBSBP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#define ONE_HUNDRED 100

/*
 * Class Methods
 */
static void FlipColors(Widget);
static void XawSmeBSBClassInitialize(void);
static void XawSmeBSBInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawSmeBSBDestroy(Widget);
static XtGeometryResult XawSmeBSBQueryGeometry(Widget, XtWidgetGeometry*,
					       XtWidgetGeometry*);
static void XawSmeBSBRedisplay(Widget, XEvent*, Region);
static Boolean XawSmeBSBSetValues(Widget, Widget, Widget,
				  ArgList, Cardinal*);

/*
 * Prototypes
 */
static void CreateGCs(Widget);
static void GetBitmapInfo(Widget, Bool);
static void GetDefaultSize(Widget, Dimension*, Dimension*);
static void DestroyGCs(Widget);
static void DrawBitmaps(Widget, GC);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(SmeBSBRec, sme_bsb.field)
static XtResource resources[] = {
  {
    XtNlabel,
    XtCLabel,
    XtRString,
    sizeof(String),
    offset(label),
    XtRString,
    NULL
  },
  {
    XtNvertSpace,
    XtCVertSpace,
    XtRInt,
    sizeof(int),
    offset(vert_space),
    XtRImmediate,
    (XtPointer)25
  },
  {
    XtNleftBitmap,
    XtCLeftBitmap,
    XtRBitmap,
    sizeof(Pixmap),
    offset(left_bitmap),
    XtRImmediate,
    (XtPointer)None
  },
  {
    XtNjustify,
    XtCJustify,
    XtRJustify,
    sizeof(XtJustify),
    offset(justify),
    XtRImmediate,
    (XtPointer)XtJustifyLeft
  },
  {
    XtNrightBitmap,
    XtCRightBitmap,
    XtRBitmap,
    sizeof(Pixmap),
    offset(right_bitmap),
    XtRImmediate,
    (XtPointer)None
  },
  {
    XtNleftMargin,
    XtCHorizontalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(left_margin),
    XtRImmediate,
    (XtPointer)4
  },
  {
    XtNrightMargin,
    XtCHorizontalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(right_margin),
    XtRImmediate,
    (XtPointer)4
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
#ifndef OLDXAW
  {
    XtNmenuName,
    XtCMenuName,
    XtRString,
    sizeof(String),
    offset(menu_name),
    XtRImmediate,
    (XtPointer)NULL
  },
#endif
};
#undef offset

#define superclass (&smeClassRec)
SmeBSBClassRec smeBSBClassRec = {
  /* rectangle */
  {
    (WidgetClass)superclass,		/* superclass */
    "SmeBSB",				/* class_name */
    sizeof(SmeBSBRec),			/* size */
    XawSmeBSBClassInitialize,		/* class_init */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawSmeBSBInitialize,		/* initialize */
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
    XawSmeBSBDestroy,			/* destroy */
    NULL,				/* resize */
    XawSmeBSBRedisplay,			/* expose */
    XawSmeBSBSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* intrinsics version */
    NULL,				/* callback offsets */
    NULL,				/* tm_table */
    XawSmeBSBQueryGeometry,		/* query_geometry */
    NULL,				/* display_accelerator */
    NULL,				/* extension */
  },
  /* sme */
  {
    FlipColors,				/* highlight */
    FlipColors,				/* unhighlight */
    XtInheritNotify,			/* notify */
    NULL,				/* extension */
  },
  /* sme_bsb */
  {
    NULL,				/* extension */
  },
};
WidgetClass smeBSBObjectClass = (WidgetClass)&smeBSBClassRec;

/*
 * Function:
 *	XawSmeBSBClassInitialize
 *
 * Description:
 *	Initializes the SmeBSBObject.
 */
static void
XawSmeBSBClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtAddConverter(XtRString, XtRJustify, XmuCvtStringToJustify, NULL, 0);
    XtSetTypeConverter(XtRJustify, XtRString, XmuCvtJustifyToString,
		       NULL, 0, XtCacheNone, NULL);
}

/*
 * Function:
 *	XawSmeBSBInitialize
 *
 * Parameters:
 *	request	- widget requested by the argument list
 *	cnew	- new widget with both resource and non resource values
 *
 * Description:
 *	Initializes the simple menu widget entry.
 */
/*ARGSUSED*/
static void
XawSmeBSBInitialize(Widget request _X_UNUSED, Widget cnew,
		    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SmeBSBObject entry = (SmeBSBObject)cnew;

    if (!entry->sme_bsb.font) XtError("Aborting: no font found\n");

    if (entry->sme_bsb.label == NULL)
	entry->sme_bsb.label = XtName(cnew);
    else
	entry->sme_bsb.label = XtNewString(entry->sme_bsb.label);

    GetDefaultSize(cnew, &entry->rectangle.width, &entry->rectangle.height);
    CreateGCs(cnew);

    entry->sme_bsb.left_bitmap_width = entry->sme_bsb.left_bitmap_height = 0;
    entry->sme_bsb.right_bitmap_width = entry->sme_bsb.right_bitmap_height = 0;

    GetBitmapInfo(cnew, True);	/* Left Bitmap Info */
    GetBitmapInfo(cnew, False);	/* Right Bitmap Info */
}

/*
 * Function:
 *	XawSmeBSBDestroy
 *
 * Parameters:
 *	w - simple menu widget entry
 */
static void
XawSmeBSBDestroy(Widget w)
{
    SmeBSBObject entry = (SmeBSBObject)w;

    DestroyGCs(w);
    if (entry->sme_bsb.label != XtName(w))
	XtFree((char *)entry->sme_bsb.label);
}

/*
 * Function:
 *	XawSmeBSBRedisplay
 *
 * Parameters:
 *	w      - simple menu widget entry
 *	event  - X event that caused this redisplay
 *	region - region the needs to be repainted
 *
 * Description:
 *	Redisplays the contents of the widget.
 */
/* ARGSUSED */
static void
XawSmeBSBRedisplay(Widget w, XEvent *event _X_UNUSED, Region region _X_UNUSED)
{
    GC gc;
    SmeBSBObject entry = (SmeBSBObject)w;
    int	font_ascent, font_descent, y_loc;
    int	fontset_ascent, fontset_descent;
    XFontSetExtents *ext = XExtentsOfFontSet(entry->sme_bsb.fontset);

    font_ascent = font_descent = fontset_ascent = fontset_descent = 0;
    entry->sme_bsb.set_values_area_cleared = False;

    if (entry->sme.international == True) {
	fontset_ascent = XawAbs(ext->max_ink_extent.y);
	fontset_descent = ext->max_ink_extent.height - fontset_ascent;
    }
    else {
	font_ascent = entry->sme_bsb.font->max_bounds.ascent;
	font_descent = entry->sme_bsb.font->max_bounds.descent;
    }
    y_loc = XtY(entry);

    if (XtIsSensitive(w) && XtIsSensitive(XtParent(w))) {
	if (w == XawSimpleMenuGetActiveEntry(XtParent(w))) {
	    XFillRectangle(XtDisplayOfObject(w), XtWindowOfObject(w),
			   entry->sme_bsb.norm_gc, XtX(w), y_loc,
			   XtWidth(entry), XtHeight(entry));
	    gc = entry->sme_bsb.rev_gc;
	}
	else
	    gc = entry->sme_bsb.norm_gc;
    }
    else
	gc = entry->sme_bsb.norm_gray_gc;

    if (entry->sme_bsb.label != NULL) {
	int x_loc = entry->sme_bsb.left_margin;
	int len = (int)strlen(entry->sme_bsb.label);
	_Xconst char *label = entry->sme_bsb.label;
	 int width, t_width;

	switch(entry->sme_bsb.justify) {
	    case XtJustifyCenter:
		if (entry->sme.international == True) {
		    t_width = XmbTextEscapement(entry->sme_bsb.fontset,label,
						len);
		    width = XtWidth(entry) - (entry->sme_bsb.left_margin +
					      entry->sme_bsb.right_margin);
		}
		else {
		    t_width = XTextWidth(entry->sme_bsb.font, label, len);
		    width = XtWidth(entry) - (entry->sme_bsb.left_margin +
					      entry->sme_bsb.right_margin);
		}
		x_loc += (width - t_width) >> 1;
		break;
	    case XtJustifyRight:
		if (entry->sme.international == True) {
		    t_width = XmbTextEscapement(entry->sme_bsb.fontset,label,
						len);
		    x_loc = XtWidth(entry) - (entry->sme_bsb.right_margin +
					      t_width);
		}
		else {
		    t_width = XTextWidth(entry->sme_bsb.font, label, len);
		    x_loc = XtWidth(entry) - (entry->sme_bsb.right_margin +
					      t_width);
		}
		break;
	    case XtJustifyLeft:
		/*FALLTHROUGH*/
	    default:
		break;
	}

	/* this will center the text in the gadget top-to-bottom */
	if (entry->sme.international == True) {
	    y_loc += ((XtHeight(entry) -
		      (fontset_ascent + fontset_descent)) >> 1) +
		       fontset_ascent;

	    XmbDrawString(XtDisplayOfObject(w), XtWindowOfObject(w),
		          entry->sme_bsb.fontset, gc,
			  XtX(w) + x_loc, y_loc, label, len);
	}
	else {
	    y_loc += ((XtHeight(entry) -
		      (font_ascent + font_descent)) >> 1) + font_ascent;

	    XDrawString(XtDisplayOfObject(w), XtWindowOfObject(w), gc,
			XtX(w) + x_loc, y_loc, label, len);
	}
    }

    DrawBitmaps(w, gc);
}


/*
 * Function:
 *	XawSmeBSBSetValues
 *
 * Parameters:
 *	current	- current state of the widget
 *	request	- what was requested
 *	cnew	- what the widget will become
 *
 * Description:
 *	Relayout the menu when one of the resources is changed.
 */

/*ARGSUSED*/
static Boolean
XawSmeBSBSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SmeBSBObject entry = (SmeBSBObject)cnew;
    SmeBSBObject old_entry = (SmeBSBObject)current;
    Boolean ret_val = False;

    if (old_entry->sme_bsb.label != entry->sme_bsb.label) {
	if (old_entry->sme_bsb.label != XtName(cnew))
	    XtFree((char *)old_entry->sme_bsb.label);

	if (entry->sme_bsb.label != XtName(cnew))
	    entry->sme_bsb.label = XtNewString(entry->sme_bsb.label);

	ret_val = True;
    }

    if (entry->rectangle.sensitive != old_entry->rectangle.sensitive)
	ret_val = True;

    if (entry->sme_bsb.left_bitmap != old_entry->sme_bsb.left_bitmap) {
	GetBitmapInfo(cnew, True);
	ret_val = True;
    }

    if (entry->sme_bsb.right_bitmap != old_entry->sme_bsb.right_bitmap) {
	GetBitmapInfo(cnew, False);
	ret_val = True;
    }

    if ((old_entry->sme_bsb.font != entry->sme_bsb.font
	 && old_entry->sme.international == False)
	|| old_entry->sme_bsb.foreground != entry->sme_bsb.foreground)  {
	DestroyGCs(current);
	CreateGCs(cnew);
	ret_val = True;
    }

    if (old_entry->sme_bsb.fontset != entry->sme_bsb.fontset &&
	old_entry->sme.international == True)
	/* DONT changes the GCs, because the fontset is not in them */
	ret_val = True;

    if (ret_val) {
	Dimension width, height;

	GetDefaultSize(cnew, &width, &height);
	entry->sme_bsb.set_values_area_cleared = True;
	XtMakeResizeRequest(cnew, width, height, NULL, NULL);
    }

    return (ret_val);
}

/*
 * Function:
 *	XawSmeBSBQueryGeometry
 *
 * Parameters:
 *	w	   - menu entry object
 *	itended	   - intended and return geometry info
 *	return_val - ""
 *
 * Returns:
 *	Geometry Result
 *
 * Description:
 *	  Returns the preferred geometry for this widget.
 *	  See the Intrinsics manual for details on what this function is for.
 */
static XtGeometryResult
XawSmeBSBQueryGeometry(Widget w, XtWidgetGeometry *intended,
		       XtWidgetGeometry *return_val)
{
    SmeBSBObject entry = (SmeBSBObject)w;
    Dimension width, height;
    XtGeometryResult ret_val = XtGeometryYes;
    XtGeometryMask mode = intended->request_mode;

    GetDefaultSize(w, &width, &height);

    if (((mode & CWWidth) && intended->width != width) || !(mode & CWWidth)) {
	return_val->request_mode |= CWWidth;
	return_val->width = width;
	ret_val = XtGeometryAlmost;
    }

    if (((mode & CWHeight) && intended->height != height) || !(mode & CWHeight)) {
	return_val->request_mode |= CWHeight;
	return_val->height = height;
	ret_val = XtGeometryAlmost;
    }

    if (ret_val == XtGeometryAlmost) {
	mode = return_val->request_mode;
	if (((mode & CWWidth) && width == XtWidth(entry)) &&
	    ((mode & CWHeight) && height == XtHeight(entry)))
	    return (XtGeometryNo);
    }

    return (ret_val);
}

/*
 * Function:
 *	FlipColors
 *
 * Parameters:
 *	w - bsb menu entry widget
 *
 * Description:
 *	Invert the colors of the current entry.
 */
static void
FlipColors(Widget w)
{
    SmeBSBObject entry = (SmeBSBObject)w;

    if (entry->sme_bsb.set_values_area_cleared)
	return;

    XFillRectangle(XtDisplayOfObject(w), XtWindowOfObject(w),
		   entry->sme_bsb.invert_gc,
		   XtX(w), XtY(entry), XtWidth(entry), XtHeight(entry));
}

/*
 * Function:
 *	GetDefaultSize
 *
 * Parameters:
 *	w      - menu entry widget.
 *	width  - default width (return)
 *	height - default height (return)
 *
 * Description:
 *	Calculates the Default (preferred) size of this menu entry.
 */
static void
GetDefaultSize(Widget w, Dimension *width, Dimension *height)
{
    SmeBSBObject entry = (SmeBSBObject)w;

    if (entry->sme.international == True) {
	XFontSetExtents *ext = XExtentsOfFontSet(entry->sme_bsb.fontset);

	if (entry->sme_bsb.label == NULL)
	    *width = 0;
	else
	    *width = (Dimension)XmbTextEscapement(entry->sme_bsb.fontset,
				       entry->sme_bsb.label,
				       (int)strlen(entry->sme_bsb.label));
	*width = (Dimension)(*width + (entry->sme_bsb.left_margin + entry->sme_bsb.right_margin));
	*height = ext->max_ink_extent.height;
	*height = (Dimension)(((int)*height * (ONE_HUNDRED +
				   entry->sme_bsb.vert_space)) / ONE_HUNDRED);
    }
    else {
	if (entry->sme_bsb.label == NULL)
	    *width = 0;
	else
	    *width = (Dimension)XTextWidth(entry->sme_bsb.font, entry->sme_bsb.label,
			    (int)strlen(entry->sme_bsb.label));

	*width = (Dimension)(*width + (entry->sme_bsb.left_margin + entry->sme_bsb.right_margin));

	*height = (Dimension)(entry->sme_bsb.font->max_bounds.ascent +
		  entry->sme_bsb.font->max_bounds.descent);

	*height = (Dimension)(((int)*height * (ONE_HUNDRED +
				   entry->sme_bsb.vert_space)) / ONE_HUNDRED);
    }
}

/*
 * Function:
 *	DrawBitmaps
 *
 * Parameters:
 *	w  - simple menu widget entry
 *	gc - graphics context to use for drawing
 *
 * Description:
 *	Draws left and right bitmaps.
 */
static void
DrawBitmaps(Widget w, GC gc)
{
    int x_loc, y_loc;
    SmeBSBObject entry = (SmeBSBObject)w;

    if (entry->sme_bsb.left_bitmap == None &&
	entry->sme_bsb.right_bitmap == None)
    return;

    /*
     * Draw Left Bitmap
     */
    if (entry->sme_bsb.left_bitmap != None) {
	x_loc = ((entry->sme_bsb.left_margin -
		  entry->sme_bsb.left_bitmap_width) >> 1) + XtX(w);

	y_loc = XtY(entry) + ((XtHeight(entry) -
			       entry->sme_bsb.left_bitmap_height) >> 1);

	XCopyPlane(XtDisplayOfObject(w), entry->sme_bsb.left_bitmap,
		   XtWindowOfObject(w), gc, 0, 0,
		   entry->sme_bsb.left_bitmap_width,
		   entry->sme_bsb.left_bitmap_height, x_loc, y_loc, 1);
    }

    /*
     * Draw Right Bitmap
     */
    if (entry->sme_bsb.right_bitmap != None) {
	x_loc = XtWidth(entry) - ((entry->sme_bsb.right_margin +
				  entry->sme_bsb.right_bitmap_width) >> 1) +
				  XtX(w);
	y_loc = XtY(entry) + ((XtHeight(entry) -
			      entry->sme_bsb.right_bitmap_height) >> 1);

	XCopyPlane(XtDisplayOfObject(w), entry->sme_bsb.right_bitmap,
		   XtWindowOfObject(w), gc, 0, 0,
		   entry->sme_bsb.right_bitmap_width,
	 	   entry->sme_bsb.right_bitmap_height, x_loc, y_loc, 1);
    }
}

/*
 * Function:
 *	GetBitmapInfo
 *
 * Parameters:
 *	w	- bsb menu entry object
 *	is_left - True: if we are testing left bitmap
 *		  False: if we are testing the right bitmap
 *
 * Description:
 *	Gets the bitmap information from either of the bitmaps.
 */
static void
GetBitmapInfo(Widget w, Bool is_left)
{
    SmeBSBObject entry = (SmeBSBObject)w;
    unsigned int depth, bw;
    Window root;
    int x, y;
    unsigned int width, height;

    if (is_left) {
	if (entry->sme_bsb.left_bitmap != None &&
	    XGetGeometry(XtDisplayOfObject(w),
			 entry->sme_bsb.left_bitmap, &root,
			 &x, &y, &width, &height, &bw, &depth))	{
	    entry->sme_bsb.left_bitmap_width = (Dimension)width;
	    entry->sme_bsb.left_bitmap_height = (Dimension)height;
	}
    }
    else if (entry->sme_bsb.right_bitmap != None &&
	     XGetGeometry(XtDisplayOfObject(w),
			  entry->sme_bsb.right_bitmap, &root,
			  &x, &y, &width, &height, &bw, &depth)) {
	entry->sme_bsb.right_bitmap_width = (Dimension)width;
	entry->sme_bsb.right_bitmap_height = (Dimension)height;
    }
}

/*
 * Function:
 *	CreateGCs
 *
 * Parameters:
 *	w - simple menu widget entry
 *
 * Description:
 *	Creates all gc's for the simple menu widget.
 */
static void
CreateGCs(Widget w)
{
    SmeBSBObject entry = (SmeBSBObject)w;
    XGCValues values;
    XtGCMask mask, mask_i18n;

    values.foreground = XtParent(w)->core.background_pixel;
    values.background = entry->sme_bsb.foreground;
    values.font = entry->sme_bsb.font->fid;
    values.graphics_exposures = False;
    mask      = GCForeground | GCBackground | GCGraphicsExposures | GCFont;
    mask_i18n = GCForeground | GCBackground | GCGraphicsExposures;
    if (entry->sme.international == True)
	entry->sme_bsb.rev_gc = XtAllocateGC(w, 0, mask_i18n, &values, GCFont, 0);
    else
	entry->sme_bsb.rev_gc = XtGetGC(w, mask, &values);

    values.foreground = entry->sme_bsb.foreground;
    values.background = XtParent(w)->core.background_pixel;
    if (entry->sme.international == True)
	entry->sme_bsb.norm_gc = XtAllocateGC(w, 0, mask_i18n, &values, GCFont, 0);
    else
	entry->sme_bsb.norm_gc = XtGetGC(w, mask, &values);

    values.fill_style = FillTiled;
    values.tile   = XmuCreateStippledPixmap(XtScreenOfObject(w),
					    entry->sme_bsb.foreground,
					    XtParent(w)->core.background_pixel,
					    XtParent(w)->core.depth);
    values.graphics_exposures = False;
    mask |= GCTile | GCFillStyle;
    mask_i18n |= GCTile | GCFillStyle;
    if (entry->sme.international == True)
	entry->sme_bsb.norm_gray_gc = XtAllocateGC(w, 0, mask_i18n, &values,
						   GCFont, 0);
    else
	entry->sme_bsb.norm_gray_gc = XtGetGC(w, mask, &values);

    values.foreground ^= values.background;
    values.background = 0;
    values.function = GXxor;
    mask = GCForeground | GCBackground | GCGraphicsExposures | GCFunction;
    entry->sme_bsb.invert_gc = XtGetGC(w, mask, &values);
}

/*
 * Function:
 *	DestroyGCs
 *
 * Parameters:
 *	w - simple menu widget entry
 *
 * Description:
 *	Removes all gc's for the simple menu widget.
 */
static void
DestroyGCs(Widget w)
{
    SmeBSBObject entry = (SmeBSBObject)w;

    XtReleaseGC(w, entry->sme_bsb.norm_gc);
    XtReleaseGC(w, entry->sme_bsb.norm_gray_gc);
    XtReleaseGC(w, entry->sme_bsb.rev_gc);
    XtReleaseGC(w, entry->sme_bsb.invert_gc);
}
