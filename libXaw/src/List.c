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
 * List.c - List widget
 *
 * This is a List widget.  It allows the user to select an item in a list and
 * notifies the application through a callback function.
 *
 *	Created:	8/13/88
 *	By:		Chris D. Peterson
 *                      MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <ctype.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xaw/ListP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#define HeightLock  1
#define WidthLock   2
#define LongestLock 4

#define HeightFree(w)	!(((ListWidget)(w))->list.freedoms & HeightLock)
#define WidthFree(w)	!(((ListWidget)(w))->list.freedoms & WidthLock)
#define LongestFree(w)	!(((ListWidget)(w))->list.freedoms & LongestLock)

#define MaxSize 32767

/*
 * Class Methods
 */
static void XawListDestroy(Widget);
static void XawListInitialize(Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult XawListQueryGeometry(Widget, XtWidgetGeometry*,
					     XtWidgetGeometry*);
static void XawListRedisplay(Widget, XEvent*, Region);
static void XawListResize(Widget);
static Boolean XawListSetValues(Widget, Widget, Widget, ArgList, Cardinal*);

/*
 * Prototypes
 */
static void CalculatedValues(Widget);
static void ChangeSize(Widget, unsigned int, unsigned int);
static void ClipToShadowInteriorAndLongest(ListWidget, GC*, unsigned int);
static int CvtToItem(Widget, int, int, int*);
static void FindCornerItems(Widget, XEvent*, int*, int*);
static void GetGCs(Widget);
static void HighlightBackground(Widget, int, int, GC);
static Bool ItemInRectangle(Widget, int, int, int);
static Bool Layout(Widget, Bool, Bool, Dimension*, Dimension*);
static void PaintItemName(Widget, int);
static void ResetList(Widget, Bool, Bool);

/*
 * Actions
 */
static void Notify(Widget, XEvent*, String*, Cardinal*);
static void Set(Widget, XEvent*, String*, Cardinal*);
static void Unset(Widget, XEvent*, String*, Cardinal*);

/*
 * Initialization
 */
static char defaultTranslations[] =
"<Btn1Down>:"	"Set()\n"
"<Btn1Up>:"	"Notify()\n"
;

#define offset(field) XtOffsetOf(ListRec, field)
static XtResource resources[] = {
  {
    XtNforeground,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    offset(list.foreground),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNcursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(simple.cursor),
    XtRString,
    (XtPointer)"left_ptr"
  },
  {
    XtNfont,
    XtCFont,
    XtRFontStruct,
    sizeof(XFontStruct*),
    offset(list.font),
    XtRString,
    (XtPointer)XtDefaultFont
  },
  {
    XtNfontSet,
    XtCFontSet,
    XtRFontSet,
    sizeof(XFontSet),
    offset(list.fontset),
    XtRString,
    (XtPointer)XtDefaultFontSet
  },
  {
    XtNlist,
    XtCList,
    XtRPointer,
    sizeof(char**),
    offset(list.list),
#ifdef notyet
    XtRStringArray,
#else
    XtRString,
#endif
    NULL
  },
  {
    XtNdefaultColumns,
    XtCColumns,
    XtRInt,
    sizeof(int),
    offset(list.default_cols),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNlongest,
    XtCLongest,
    XtRInt,
    sizeof(int),
    offset(list.longest),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNnumberStrings,
    XtCNumberStrings,
    XtRInt,
    sizeof(int),
    offset(list.nitems),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNpasteBuffer,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(list.paste),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNforceColumns,
    XtCColumns,
    XtRBoolean,
    sizeof(Boolean),
    offset(list.force_cols),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNverticalList,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(list.vertical_cols),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNinternalWidth,
    XtCWidth,
    XtRDimension,
    sizeof(Dimension),
    offset(list.internal_width),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNinternalHeight,
    XtCHeight,
    XtRDimension,
    sizeof(Dimension),
    offset(list.internal_height),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNcolumnSpacing,
    XtCSpacing,
    XtRDimension,
    sizeof(Dimension),
    offset(list.column_space),
    XtRImmediate,
    (XtPointer)6
  },
  {
    XtNrowSpacing,
    XtCSpacing,
    XtRDimension,
    sizeof(Dimension),
    offset(list.row_space),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNcallback,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(list.callback),
    XtRCallback,
    NULL
  },
#ifndef OLDXAW
  {
    XtNshowCurrent,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(list.show_current),
    XtRImmediate,
    (XtPointer)False
  },
#endif
};
#undef offset

static XtActionsRec actions[] = {
      {"Notify",	Notify},
      {"Set",		Set},
      {"Unset",		Unset},
};

#define Superclass (&simpleClassRec)
ListClassRec listClassRec = {
  /* core */
  {
    (WidgetClass)Superclass,		/* superclass */
    "List",				/* class_name */
    sizeof(ListRec),			/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawListInitialize,			/* initialize */
    NULL,				/* initialize_hook */
    XtInheritRealize,			/* realize */
    actions,				/* actions */
    XtNumber(actions),			/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    False,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawListDestroy,			/* destroy */
    XawListResize,			/* resize */
    XawListRedisplay,			/* expose */
    XawListSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    defaultTranslations,		/* tm_table */
    XawListQueryGeometry,		/* query_geometry */
  },
  /* simple */
  {
    XtInheritChangeSensitive,		/* change_sensitive */
  },
  /* list */
  {
    NULL,				/* extension */
  },
};

WidgetClass listWidgetClass = (WidgetClass)&listClassRec;

/*
 * Implementation
 */
static void
GetGCs(Widget w)
{
    XGCValues	values;
    ListWidget lw = (ListWidget)w;

    values.foreground	= lw->list.foreground;
    values.font		= lw->list.font->fid;

    if (lw->simple.international == True)
	lw->list.normgc = XtAllocateGC(w, 0, GCForeground, &values, GCFont, 0);
    else
	lw->list.normgc = XtGetGC(w, GCForeground | GCFont, &values);

    values.foreground	= lw->core.background_pixel;

    if (lw->simple.international == True)
	lw->list.revgc = XtAllocateGC(w, 0, GCForeground, &values, GCFont, 0);
    else
	lw->list.revgc = XtGetGC(w, GCForeground | GCFont, &values);

    values.tile       = XmuCreateStippledPixmap(XtScreen(w),
						lw->list.foreground,
						lw->core.background_pixel,
						lw->core.depth);
    values.fill_style = FillTiled;

    if (lw->simple.international == True)
	lw->list.graygc = XtAllocateGC(w, 0, GCTile | GCFillStyle,
				       &values, GCFont, 0);
    else
	lw->list.graygc = XtGetGC(w, GCFont | GCTile | GCFillStyle, &values);
}

static void
CalculatedValues(Widget w)
{
    int i, len;
    ListWidget lw = (ListWidget)w;

    /* If list is NULL then the list will just be the name of the widget */
    if (lw->list.list == NULL) {
	lw->list.list = &lw->core.name;
	lw->list.nitems = 1;
    }

    /* Get number of items */
    if (lw->list.nitems == 0)
	for (; lw->list.list[lw->list.nitems] != NULL ; lw->list.nitems++)
	    ;

    /* Get column width */
    if (LongestFree(lw)) {
	lw->list.longest = 0; /* so it will accumulate real longest below */

	for (i = 0 ; i < lw->list.nitems; i++) {
	    if (lw->simple.international == True)
		len = XmbTextEscapement(lw->list.fontset, lw->list.list[i],
					(int)strlen(lw->list.list[i]));
	    else
		len = XTextWidth(lw->list.font, lw->list.list[i],
				 (int)strlen(lw->list.list[i]));
	    if (len > lw->list.longest)
		lw->list.longest = len;
	}
    }

    lw->list.col_width = lw->list.longest + lw->list.column_space;
}

/*
 * Function:
 *	ResetList
 *
 * Parameters:
 *	w	- list widget
 *	changex - allow the height or width to change?
 *	changey - ""
 *
 * Description:
 *	Resets the new list when important things change.
 *
 * Returns:
 *	True if width or height have been changed
 */
static void
ResetList(Widget w, Bool changex, Bool changey)
{
    Dimension width = XtWidth(w);
    Dimension height = XtHeight(w);

    CalculatedValues(w);

    if (Layout(w, changex, changey, &width, &height)) {
	if (XtIsComposite(XtParent(w)))
	    ChangeSize(w, width, height);
	else {
	    XtWidth(w) = width;
	    XtHeight(w) = height;
	}
    }
}

/*
 * Function:
 *	ChangeSize
 *
 * Parameters:
 *	w - widget to try change the size of
 *
 * Description:
 *	Laysout the widget.
 */
static void
ChangeSize(Widget w, unsigned int width, unsigned int height)
{
    XtWidgetGeometry request, reply;

    request.request_mode = CWWidth | CWHeight;
    request.width = (Dimension)width;
    request.height = (Dimension)height;

    switch (XtMakeGeometryRequest(w, &request, &reply)) {
	case XtGeometryYes:
	case XtGeometryNo:
	    break;
	case XtGeometryAlmost:
	    Layout(w, request.height != reply.height,
		   request.width != reply.width, &reply.width, &reply.height);
	    request = reply;
	    switch (XtMakeGeometryRequest(w, &request, &reply)) {
		case XtGeometryYes:
		case XtGeometryNo:
		    break;
		case XtGeometryAlmost:
		    request = reply;
		    Layout(w, False, False, &request.width, &request.height);
		    request.request_mode = CWWidth | CWHeight;
		    XtMakeGeometryRequest(w, &request, &reply);
		    /*FALLTHROUGH*/
		default:
		    break;
	}
	/*FALLTHROUGH*/
	default:
	    break;
    }
}

/*ARGSUSED*/
static void
XawListInitialize(Widget temp1 _X_UNUSED, Widget cnew, ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ListWidget lw = (ListWidget)cnew;

    if (!lw->list.font) XtError("Aborting: no font found\n");
    if (lw->simple.international && !lw->list.fontset)
	XtError("Aborting: no fontset found\n");

    /*
     * Initialize all private resources
     */
    /* record for posterity if we are free */
    lw->list.freedoms = ((XtWidth(lw) != 0) * WidthLock +
			 (XtHeight(lw) != 0) * HeightLock +
			 (lw->list.longest != 0) * LongestLock);

    GetGCs(cnew);

    /* Set row height, based on font or fontset */
    if (lw->simple.international == True)
	lw->list.row_height =
		XExtentsOfFontSet(lw->list.fontset)->max_ink_extent.height +
				  lw->list.row_space;
    else
	lw->list.row_height = lw->list.font->max_bounds.ascent +
			      lw->list.font->max_bounds.descent +
			      lw->list.row_space;

    ResetList(cnew, WidthFree(lw), HeightFree(lw));

    lw->list.highlight = lw->list.is_highlighted = NO_HIGHLIGHT;
}

/*
 * Function:
 *	CvtToItem
 *
 * Parameters:
 *	w    - list widget
 *	xloc - x location
 *	yloc - y location
 *
 * Description:
 *	Converts Xcoord to item number of item containing that point.
 *
 * Returns:
 *	Item number
 */
static int
CvtToItem(Widget w, int xloc, int yloc, int *item)
{
    int one, another;
    ListWidget lw = (ListWidget)w;
    int ret_val = OKAY;

    if (lw->list.vertical_cols) {
	one = lw->list.nrows * ((xloc - (int)lw->list.internal_width)
			        / lw->list.col_width);
	another = (yloc - (int)lw->list.internal_height) / lw->list.row_height;
	/* If out of range, return minimum possible value */
	if (another >= lw->list.nrows) {
	    another = lw->list.nrows - 1;
	    ret_val = OUT_OF_RANGE;
	}
    }
    else {
	one = (lw->list.ncols * ((yloc - (int)lw->list.internal_height)
				 / lw->list.row_height));
	/* If in right margin handle things right */
	another = (xloc - (int)lw->list.internal_width) / lw->list.col_width;
	if (another >= lw->list.ncols) {
	    another = lw->list.ncols - 1;
	    ret_val = OUT_OF_RANGE;
	}
    }
    if (xloc < 0 || yloc < 0)
	ret_val = OUT_OF_RANGE;
    if (one < 0)
	one = 0;
    if (another < 0)
	another = 0;
    *item = one + another;
    if (*item >= lw->list.nitems)
	return (OUT_OF_RANGE);

  return (ret_val);
}

/*
 * Function:
 *	FindCornerItems
 *
 * Arguments:
 *	w      - list widget
 *	event  - event structure that has the rectangle it it
 *	ul_ret - the corners (return)
 *	lr_ret - ""
 *
 * Description:
 *	Find the corners of the rectangle in item space.
 */
static void
FindCornerItems(Widget w, XEvent *event, int *ul_ret, int *lr_ret)
{
    int xloc, yloc;

    xloc = event->xexpose.x;
    yloc = event->xexpose.y;
    CvtToItem(w, xloc, yloc, ul_ret);
    xloc += event->xexpose.width;
    yloc += event->xexpose.height;
    CvtToItem(w, xloc, yloc, lr_ret);
}

/*
 * Function:
 *	ItemInRectangle
 *
 * Parameters:
 *	w    - list widget
 *	ul   - corners of the rectangle in item space
 *	lr   - ""
 *	item - item to check
 *
 * Returns:
 *	True if the item passed is in the given rectangle
 */
static Bool
ItemInRectangle(Widget w, int ul, int lr, int item)
{
    ListWidget lw = (ListWidget)w;
    int mod_item;
    int things;

    if (item < ul || item > lr)
      return (False);
    if (lw->list.vertical_cols)
	things = lw->list.nrows;
    else
	things = lw->list.ncols;

    mod_item = item % things;
    if ((mod_item >= ul % things) && (mod_item <= lr % things))
      return (True);

    return (False);
}

/* HighlightBackground()
 *
 * Paints the color of the background for the given item.  It performs
 * clipping to the interior of internal_width/height by hand, as its a
 * simple calculation and probably much faster than using Xlib and a clip mask.
 *
 *  x, y - ul corner of the area item occupies.
 *  gc - the gc to use to paint this rectangle
 */
static void
HighlightBackground(Widget w, int x, int y, GC gc)
{
    ListWidget lw = (ListWidget)w;
    Dimension width = (Dimension)lw->list.col_width;
    Dimension height = (Dimension)lw->list.row_height;
    Dimension frame_limited_width = (Dimension)(XtWidth(w) - lw->list.internal_width - x);
    Dimension frame_limited_height= (Dimension)(XtHeight(w) - lw->list.internal_height - y);

    /* Clip the rectangle width and height to the edge of the drawable area */
    if  (width > frame_limited_width)
	width = frame_limited_width;
    if  (height > frame_limited_height)
	height = frame_limited_height;

    /* Clip the rectangle x and y to the edge of the drawable area */
    if (x < lw->list.internal_width) {
	width = (Dimension)(width - (lw->list.internal_width - x));
	x = lw->list.internal_width;
    }
    if (y < lw->list.internal_height) {
      height = (Dimension)(height - (lw->list.internal_height - y));
	y = lw->list.internal_height;
    }

    if (gc == lw->list.revgc && lw->core.background_pixmap != XtUnspecifiedPixmap)
	XClearArea(XtDisplay(w), XtWindow(w), x, y, width, height, False);
    else
	XFillRectangle(XtDisplay(w), XtWindow(w), gc, x, y, width, height);
}


/* ClipToShadowInteriorAndLongest()
 *
 * Converts the passed gc so that any drawing done with that GC will not
 * write in the empty margin (specified by internal_width/height) (which also
 * prevents erasing the shadow.  It also clips against the value longest.
 * If the user doesn't set longest, this has no effect (as longest is the
 * maximum of all item lengths).  If the user does specify, say, 80 pixel
 * columns, though, this prevents items from overwriting other items.
 */
static void
ClipToShadowInteriorAndLongest(ListWidget lw, GC *gc_p, unsigned int x)
{
    XRectangle rect;

    rect.x = (short)x;
    rect.y = (short)lw->list.internal_height;
    rect.height = (unsigned short)(XtHeight(lw) - (lw->list.internal_height << 1));
    rect.width = (unsigned short)(XtWidth(lw) - (unsigned)lw->list.internal_width - x);
    if (rect.width > lw->list.longest)
	rect.width = (unsigned short)lw->list.longest;

    XSetClipRectangles(XtDisplay((Widget)lw), *gc_p, 0, 0, &rect, 1, YXBanded);
}

static void
PaintItemName(Widget w, int item)
{
    _Xconst char *str;
    GC gc;
    int x, y, str_y;
    ListWidget lw = (ListWidget)w;
    XFontSetExtents *ext  = XExtentsOfFontSet(lw->list.fontset);

    if (!XtIsRealized(w) || item > lw->list.nitems)
      return;

    if (lw->list.vertical_cols) {
	x = lw->list.col_width * (item / lw->list.nrows)
	  + lw->list.internal_width;
	y = lw->list.row_height * (item % lw->list.nrows)
	  + lw->list.internal_height;
    }
    else {
	x = lw->list.col_width * (item % lw->list.ncols)
	  + lw->list.internal_width;
	y = lw->list.row_height * (item / lw->list.ncols)
	  + lw->list.internal_height;
    }

    if ( lw->simple.international == True )
	str_y = y + XawAbs(ext->max_ink_extent.y);
    else
	str_y = y + lw->list.font->max_bounds.ascent;

    if (item == lw->list.is_highlighted) {
	if (item == lw->list.highlight) {
	    gc = lw->list.revgc;
	    HighlightBackground(w, x, y, lw->list.normgc);
	}
	else {
	    if (XtIsSensitive(w))
		gc = lw->list.normgc;
	    else
		gc = lw->list.graygc;
	    HighlightBackground(w, x, y, lw->list.revgc);
	    lw->list.is_highlighted = NO_HIGHLIGHT;
	}
    }
    else {
	if (item == lw->list.highlight) {
	    gc = lw->list.revgc;
	    HighlightBackground(w, x, y, lw->list.normgc);
	    lw->list.is_highlighted = item;
	}
	else {
	    if (XtIsSensitive(w))
		gc = lw->list.normgc;
	    else
		gc = lw->list.graygc;
	}
    }

    /* List's overall width contains the same number of inter-column
       column_space's as columns.  There should thus be a half
       column_width margin on each side of each column.
       The row case is symmetric */

    x += lw->list.column_space >> 1;
    str_y += lw->list.row_space >> 1;

    str =  lw->list.list[item];	/* draw it */

    ClipToShadowInteriorAndLongest(lw, &gc, (unsigned)x);

    if (lw->simple.international == True)
	XmbDrawString(XtDisplay(w), XtWindow(w), lw->list.fontset,
		      gc, x, str_y, str, (int)strlen(str));
    else
	XDrawString(XtDisplay(w), XtWindow(w), gc, x, str_y, str, (int)strlen(str));

    XSetClipMask(XtDisplay(w), gc, None);
}

static void
XawListRedisplay(Widget w, XEvent *event, Region region)
{
    int item;			/* an item to work with */
    int ul_item, lr_item;	/* corners of items we need to paint */
    ListWidget lw = (ListWidget)w;

    if (event == NULL) {
	ul_item = 0;
	lr_item = lw->list.nrows * lw->list.ncols - 1;
	XClearWindow(XtDisplay(w), XtWindow(w));
    }
    else
	FindCornerItems(w, event, &ul_item, &lr_item);

    if (Superclass->core_class.expose)
    (Superclass->core_class.expose)(w, event, region);

    for (item = ul_item; item <= lr_item && item < lw->list.nitems; item++)
	if (ItemInRectangle(w, ul_item, lr_item, item))
	    PaintItemName(w, item);
}

/* XawListQueryGeometry()
 *
 * This tells the parent what size we would like to be
 * given certain constraints.
 * w - the widget.
 * intended - what the parent intends to do with us.
 * requested - what we want to happen */
static XtGeometryResult
XawListQueryGeometry(Widget w, XtWidgetGeometry *intended,
		     XtWidgetGeometry *requested)
{
    Dimension new_width, new_height;
    Bool change, width_req, height_req;

    width_req = intended->request_mode & CWWidth;
    height_req = intended->request_mode & CWHeight;

    if (width_req)
	new_width = intended->width;
    else
	new_width = XtWidth(w);

    if (height_req)
	new_height = intended->height;
    else
	new_height = XtHeight(w);

    requested->request_mode = 0;

   /*
    * We only care about our height and width
    */
    if (!width_req && !height_req)
	return (XtGeometryYes);

    change = Layout(w, !width_req, !height_req, &new_width, &new_height);

    requested->request_mode |= CWWidth;
    requested->width = new_width;
    requested->request_mode |= CWHeight;
    requested->height = new_height;

    if (change)
	return (XtGeometryAlmost);

    return (XtGeometryYes);
}

static void
XawListResize(Widget w)
{
    Dimension width, height;

    width = XtWidth(w);
    height = XtHeight(w);

    if (Layout(w, False, False, &width, &height))
	XtAppWarning(XtWidgetToApplicationContext(w),
		     "List Widget: Size changed when it shouldn't "
		     "have when resising.");
}

/* Layout()
 *
 * lays out the item in the list.
 * w - the widget.
 * xfree, yfree - True if we are free to resize the widget in
 *		this direction.
 * width, height- the is the current width and height that we are going
 *		we are going to layout the list widget to,
 *		depending on xfree and yfree of course.
 *
 * Return:
 *	True if width or height have been changed */
static Bool
Layout(Widget w, Bool xfree, Bool yfree, Dimension *width, Dimension *height)
{
    ListWidget lw = (ListWidget)w;
    Bool change = False;
    unsigned long width2 = 0, height2 = 0;

    /*
     * If force columns is set then always use number of columns specified
     * by default_cols
     */
    if (lw->list.force_cols) {
	lw->list.ncols = lw->list.default_cols;
	if (lw->list.ncols <= 0)
	    lw->list.ncols = 1;
	lw->list.nrows = ((lw->list.nitems - 1) / lw->list.ncols) + 1;
	if (xfree) {
	    /* this counts the same number
	       of inter-column column_space 's as columns.  There should thus
	       be a half column_space margin on each side of each column...*/
	    width2 = (unsigned long)(lw->list.ncols * lw->list.col_width +
		     (lw->list.internal_width << 1));
	    change = True;
	}
	if (yfree) {
	    height2 = (unsigned long)(lw->list.nrows * lw->list.row_height +
		      (lw->list.internal_height << 1));
	    change = True;
	}
    }

    /*
     * If both width and height are free to change the use default_cols
     * to determine the number columns and set new width and height to
     * just fit the window
     */
    else if (xfree && yfree) {
	lw->list.ncols = lw->list.default_cols;
	if (lw->list.ncols <= 0) {
	    int wid = (int)XtWidth(lw) - (int)(lw->list.internal_width << 1)
	      + (int)lw->list.column_space;

	    if (wid <= 0 || lw->list.col_width <= 0
		|| (lw->list.ncols = wid / lw->list.col_width) <= 0)
		lw->list.ncols = 1;
	}
	width2 = (unsigned long)((lw->list.ncols * lw->list.col_width)
		  + (lw->list.internal_width << 1));
	height2 = (unsigned long)((lw->list.nrows * lw->list.row_height)
		   + (lw->list.internal_height << 1));
	change = True;
    }

    /*
     * If the width is fixed then use it to determine the number of columns.
     * If the height is free to move (width still fixed) then resize the height
     * of the widget to fit the current list exactly
     */
    else if (!xfree) {
	lw->list.ncols = ((int)(*width - (lw->list.internal_width << 1))
			  / (int)lw->list.col_width);
	if (lw->list.ncols <= 0)
	    lw->list.ncols = 1;
	lw->list.nrows = ((lw->list.nitems - 1) / lw->list.ncols) + 1;
	if (yfree) {
	    height2 = (unsigned long)((lw->list.nrows * lw->list.row_height) +
		      (lw->list.internal_height << 1));
	    change = True;
	}
    }

    /*
     * The last case is xfree and !yfree we use the height to determine
     * the number of rows and then set the width to just fit the resulting
     * number of columns
     */
    else if (!yfree) {
	lw->list.nrows = ((int)(*height - (lw->list.internal_height << 1))
			  / (int)lw->list.row_height);
	if (lw->list.nrows <= 0)
	    lw->list.nrows = 1;
	lw->list.ncols = ((lw->list.nitems - 1) / lw->list.nrows) + 1;
	width2 = (unsigned long)((lw->list.ncols * lw->list.col_width) +
		 (lw->list.internal_width << 1));
	change = True;
    }

    if (!lw->list.force_cols && lw->list.nrows) {
	/*CONSTCOND*/
	while (1) {
	    lw->list.nrows = ((lw->list.nitems - 1) / lw->list.ncols) + 1;
	    width2 = (unsigned long)((lw->list.ncols * lw->list.col_width) +
		     (lw->list.internal_width << 1));
	    height2 = (unsigned long)((lw->list.nrows * lw->list.row_height) +
		      (lw->list.internal_height << 1));
	    if (width2 >= MaxSize && height2 >= MaxSize)
		break;
	    if (height2 > MaxSize)
		++lw->list.ncols;
	    else if (width2 > MaxSize && lw->list.ncols > 1)
		--lw->list.ncols;
	    else
		break;
	}
    }
    if (width2)
	*width = (Dimension)width2;
    if (height2)
	*height = (Dimension)height2;

    return (change);
}

/* Notify() - Action
 *
 * Notifies the user that a button has been pressed, and
 * calls the callback; if the XtNpasteBuffer resource is true
 * then the name of the item is also put in CUT_BUFFER0 */
/*ARGSUSED*/
static void
Notify(Widget w, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    ListWidget lw = (ListWidget)w;
    int item, item_len;
    XawListReturnStruct ret_value;

    /*
     * Find item and if out of range then unhighlight and return
     *
     * If the current item is unhighlighted then the user has aborted the
     * notify, so unhighlight and return
     */
    if ((CvtToItem(w, event->xbutton.x, event->xbutton.y, &item) == OUT_OF_RANGE)
	|| lw->list.highlight != item) {
#ifndef OLDXAW
	if (!lw->list.show_current || lw->list.selected == NO_HIGHLIGHT)
	    XawListUnhighlight(w);
	else
	    XawListHighlight(w, lw->list.selected);
#else
	XawListUnhighlight(w);
#endif
	return;
    }

    item_len = (int)strlen(lw->list.list[item]);

    if (lw->list.paste)		/* if XtNpasteBuffer set then paste it */
	XStoreBytes(XtDisplay(w), lw->list.list[item], item_len);

#ifndef OLDXAW
    lw->list.selected = item;
#endif
    /*
     * Call Callback function
     */
    ret_value.string = lw->list.list[item];
    ret_value.list_index = item;

    XtCallCallbacks(w, XtNcallback, (XtPointer)&ret_value);
}

/* Unset() - Action
 *
 * unhighlights the current element */
/*ARGSUSED*/
static void
Unset(Widget w, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    XawListUnhighlight(w);
}

/* Set() - Action
 *
 * Highlights the current element */
/*ARGSUSED*/
static void
Set(Widget w, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    int item;
    ListWidget lw = (ListWidget)w;

#ifndef OLDXAW
    lw->list.selected = lw->list.highlight;
#endif
    if (CvtToItem(w, event->xbutton.x, event->xbutton.y, &item) == OUT_OF_RANGE)
	XawListUnhighlight(w);			/* Unhighlight current item */
    else if (lw->list.is_highlighted != item)	/* If this item is not */
	XawListHighlight(w, item);		/* highlighted then do it */
}

/*
 * Set specified arguments into widget
 */
/*ARGSUSED*/
static Boolean
XawListSetValues(Widget current, Widget request, Widget cnew,
		 ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ListWidget cl = (ListWidget)current;
    ListWidget rl = (ListWidget)request;
    ListWidget nl = (ListWidget)cnew;
    Bool redraw = False;
    XFontSetExtents *ext = XExtentsOfFontSet(nl->list.fontset);

    /* If the request height/width is different, lock it.  Unless its 0. If
       neither new nor 0, leave it as it was.  Not in R5 */
    if (XtWidth(nl) != XtWidth(cl))
	nl->list.freedoms |= WidthLock;
    if (XtWidth(nl) == 0)
	nl->list.freedoms &= ~WidthLock;

    if (XtHeight(nl) != XtHeight(cl))
	nl->list.freedoms |= HeightLock;
    if (XtHeight(nl) == 0)
	nl->list.freedoms &= ~HeightLock;

    if (nl->list.longest != cl->list.longest)
	nl->list.freedoms |= LongestLock;
    if (nl->list.longest == 0)
	nl->list.freedoms &= ~LongestLock;

    if (cl->list.foreground != nl->list.foreground ||
	cl->core.background_pixel != nl->core.background_pixel ||
	cl->list.font != nl->list.font) {
	XGCValues values;

	XGetGCValues(XtDisplay(current), cl->list.graygc, GCTile, &values);
	XmuReleaseStippledPixmap(XtScreen(current), values.tile);
	XtReleaseGC(current, cl->list.graygc);
	XtReleaseGC(current, cl->list.revgc);
	XtReleaseGC(current, cl->list.normgc);
	GetGCs(cnew);
	redraw = True;
    }

    if (cl->list.font != nl->list.font && cl->simple.international == False)
	nl->list.row_height = nl->list.font->max_bounds.ascent
			    + nl->list.font->max_bounds.descent
			    + nl->list.row_space;
    else if (cl->list.fontset != nl->list.fontset
	&& cl->simple.international == True)
	nl->list.row_height = ext->max_ink_extent.height + nl->list.row_space;

    /* ...If the above two font(set) change checkers above both failed, check
       if row_space was altered.  If one of the above passed, row_height will
       already have been re-calculated */
    else if (cl->list.row_space != nl->list.row_space) {
	if (cl->simple.international == True)
	    nl->list.row_height = ext->max_ink_extent.height + nl->list.row_space;
	else
	    nl->list.row_height = nl->list.font->max_bounds.ascent
				+ nl->list.font->max_bounds.descent
				+ nl->list.row_space;
    }

    if (XtWidth(cl) != XtWidth(nl) || XtHeight(cl) != XtHeight(nl)
	|| cl->list.internal_width != nl->list.internal_width
	|| cl->list.internal_height != nl->list.internal_height
	|| cl->list.column_space != nl->list.column_space
	|| cl->list.row_space != nl->list.row_space
	|| cl->list.default_cols != nl->list.default_cols
	|| (cl->list.force_cols != nl->list.force_cols
	    && rl->list.force_cols != nl->list.ncols)
	|| cl->list.vertical_cols != nl->list.vertical_cols
	|| cl->list.longest != nl->list.longest
	|| cl->list.nitems != nl->list.nitems
	|| cl->list.font != nl->list.font
	/* Equiv. fontsets might have different values, but the same fonts,
	   so the next comparison is sloppy but not dangerous  */
	|| cl->list.fontset != nl->list.fontset
	|| cl->list.list != nl->list.list) {
	CalculatedValues(cnew);
	Layout(cnew, WidthFree(nl), HeightFree(nl),
	       &nl->core.width, &nl->core.height);
	redraw = True;
    }

    if (cl->list.list != nl->list.list || cl->list.nitems != nl->list.nitems)
	nl->list.is_highlighted = nl->list.highlight = NO_HIGHLIGHT;

    if (cl->core.sensitive != nl->core.sensitive
	|| cl->core.ancestor_sensitive != nl->core.ancestor_sensitive) {
	nl->list.highlight = NO_HIGHLIGHT;
	redraw = True;
    }

    return (Boolean)(redraw);
}

static void
XawListDestroy(Widget w)
{
    ListWidget lw = (ListWidget)w;
    XGCValues values;

    XGetGCValues(XtDisplay(w), lw->list.graygc, GCTile, &values);
    XmuReleaseStippledPixmap(XtScreen(w), values.tile);
    XtReleaseGC(w, lw->list.graygc);
    XtReleaseGC(w, lw->list.revgc);
    XtReleaseGC(w, lw->list.normgc);
}

/*
 * Function:
 *	XawListChange
 *
 * Parameters:
 *	w - list widget
 *	list - new list
 *	nitems - number of items in the list
 *	longest - length (in Pixels) of the longest element in the list
 *	resize - if True the the list widget will try to resize itself
 *
 * Description:
 *	Changes the list being used and shown.
 *
 * Note:
 *	If nitems of longest are <= 0 then they will be calculated
 *	If nitems is <= 0 then the list needs to be NULL terminated
 */
void
XawListChange(Widget w, _Xconst char **list, int nitems, int longest,
#if NeedWidePrototypes
	int resize_it
#else
	Boolean resize_it
#endif
)
{
    ListWidget lw = (ListWidget)w;
    Dimension new_width = XtWidth(w);
    Dimension new_height = XtHeight(w);

    lw->list.list = list;

    if (nitems <= 0)
	nitems = 0;
    lw->list.nitems = nitems;
    if (longest <= 0)
	longest = 0;

    /* If the user passes 0 meaning "calculate it", it must be free */
    if (longest != 0)
	lw->list.freedoms |= LongestLock;
    else
	lw->list.freedoms &= ~LongestLock;

    if (resize_it)
	lw->list.freedoms &= ~WidthLock & ~HeightLock;

    lw->list.longest = longest;

    CalculatedValues(w);

    if (Layout(w, WidthFree(w), HeightFree(w), &new_width, &new_height))
	ChangeSize(w, new_width, new_height);

    lw->list.is_highlighted = lw->list.highlight = NO_HIGHLIGHT;
    if (XtIsRealized(w))
	XawListRedisplay(w, NULL, NULL);
}

void
XawListUnhighlight(Widget w)
{
    ListWidget lw = (ListWidget)w;

    lw->list.highlight = NO_HIGHLIGHT;
    if (lw->list.is_highlighted != NO_HIGHLIGHT)
    PaintItemName(w, lw->list.is_highlighted);
}

void
XawListHighlight(Widget w, int item)
{
    ListWidget lw = (ListWidget)w;

    if (XtIsSensitive(w)) {
	lw->list.highlight = item;
	if (lw->list.is_highlighted != NO_HIGHLIGHT)
	PaintItemName(w, lw->list.is_highlighted);
	PaintItemName(w, item);
    }
}

/*
 * Function:
 *	XawListShowCurrent
 *
 * Parameters:
 *	w - list widget
 *
 * Returns:
 *	Info about the currently highlighted object
 */
XawListReturnStruct *
XawListShowCurrent(Widget w)
{
    ListWidget lw = (ListWidget)w;
    XawListReturnStruct *ret_val;

    ret_val = (XawListReturnStruct *)XtMalloc(sizeof(XawListReturnStruct));

    ret_val->list_index = lw->list.highlight;
    if (ret_val->list_index == XAW_LIST_NONE)
	ret_val->string = "";
    else
	ret_val->string = lw->list.list[ret_val->list_index];

    return (ret_val);
}
