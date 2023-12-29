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
 * Author:  Chris Peterson, MIT X Consortium.
 *
 * Much code taken from X11R3 AsciiSink.
 */

/*
 * TextSink.c - TextSink object. (For use with the text widget).
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/TextP.h>
#include <X11/Xaw/TextSinkP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

/*
 * Prototypes
 */
static void XawTextSinkClassPartInitialize(WidgetClass);
static void XawTextSinkInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawTextSinkDestroy(Widget);
static Boolean XawTextSinkSetValues(Widget, Widget, Widget,
				    ArgList, Cardinal*);

static int  MaxLines(Widget, unsigned int);
static int  MaxHeight(Widget, int);
static void DisplayText(Widget, int, int, XawTextPosition, XawTextPosition,
			Bool);
static void InsertCursor(Widget, int, int, XawTextInsertState);
static void ClearToBackground(Widget, int, int, unsigned int, unsigned int);
static void FindPosition(Widget, XawTextPosition, int, int, Bool,
			 XawTextPosition*, int*, int*);
static void FindDistance(Widget, XawTextPosition, int, XawTextPosition, int*,
			 XawTextPosition*, int*);
static void Resolve(Widget, XawTextPosition, int, int, XawTextPosition*);
static void SetTabs(Widget, int, short*);
static void GetCursorBounds(Widget, XRectangle*);

#ifndef OLDXAW
static Boolean CvtStringToPropertyList(Display*, XrmValue*, Cardinal*,
				       XrmValue*, XrmValue*, XtPointer*);
static Boolean CvtPropertyListToString(Display*, XrmValue*, Cardinal*,
				       XrmValue*, XrmValue*, XtPointer*);
static Bool BeginPaint(Widget);
static Bool EndPaint(Widget);
static void SetXlfdDefaults(Display*, XawTextProperty*);
#endif

/*
 * External
 */
void _XawTextSinkClearToBackground(Widget, int, int, unsigned, unsigned);
void _XawTextSinkDisplayText(Widget, int, int, XawTextPosition, XawTextPosition,
			     Bool);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(TextSinkRec, text_sink.field)
static XtResource resources[] = {
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
    XtNbackground,
    XtCBackground,
    XtRPixel,
    sizeof(Pixel),
    offset(background),
    XtRString,
    (XtPointer)XtDefaultBackground
  },
#ifndef OLDXAW
  {
    XtNcursorColor,
    XtCColor,
    XtRPixel,
    sizeof(Pixel),
    offset(cursor_color),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XawNtextProperties,
    XawCTextProperties,
    XawRTextProperties,
    sizeof(XawTextPropertyList*),
    offset(properties),
    XtRImmediate,
    NULL
  },
#endif
};
#undef offset

#ifndef OLDXAW
static TextSinkExtRec extension_rec = {
    NULL,				/* next_extension */
    NULLQUARK,				/* record_type */
    1,					/* version */
    sizeof(TextSinkExtRec),		/* record_size */
    BeginPaint,
    NULL,
    NULL,
    EndPaint
};

static XrmQuark Qdefault;
#endif

#define Superclass	(&objectClassRec)
TextSinkClassRec textSinkClassRec = {
  /* object */
  {
    (WidgetClass)Superclass,		/* superclass */
    "TextSink",				/* class_name */
    sizeof(TextSinkRec),		/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    XawTextSinkClassPartInitialize,	/* class_part_initialize */
    False,				/* class_inited */
    XawTextSinkInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    NULL,				/* obj1 */
    NULL,				/* obj2 */
    0,					/* obj3 */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    False,				/* obj4 */
    False,				/* obj5 */
    False,				/* obj6 */
    False,				/* obj7 */
    XawTextSinkDestroy,			/* destroy */
    NULL,				/* obj8 */
    NULL,				/* obj9 */
    XawTextSinkSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    NULL,				/* obj10 */
    NULL,				/* get_values_hook */
    NULL,				/* obj11 */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* obj12 */
    NULL,				/* obj13 */
    NULL,				/* obj14 */
    NULL,				/* extension */
  },
  /* text_sink */
  {
    DisplayText,			/* DisplayText */
    InsertCursor,			/* InsertCursor */
    ClearToBackground,			/* ClearToBackground */
    FindPosition,			/* FindPosition */
    FindDistance,			/* FindDistance */
    Resolve,				/* Resolve */
    MaxLines,				/* MaxLines */
    MaxHeight,				/* MaxHeight */
    SetTabs,				/* SetTabs */
    GetCursorBounds,			/* GetCursorBounds */
  },
};

WidgetClass textSinkObjectClass = (WidgetClass)&textSinkClassRec;

/*
 * Implementation
 */
static void
XawTextSinkClassPartInitialize(WidgetClass wc)
{
    TextSinkObjectClass t_src, superC;
#ifndef OLDXAW
    static XtConvertArgRec CvtArgs[] = {
      {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.self),
       sizeof(Widget)},
    };
#endif

    t_src = (TextSinkObjectClass) wc;
    superC = (TextSinkObjectClass) t_src->object_class.superclass;

#ifndef OLDXAW
    extension_rec.record_type = XrmPermStringToQuark("TextSink");
    extension_rec.next_extension = (XtPointer)t_src->text_sink_class.extension;
    t_src->text_sink_class.extension = &extension_rec;

    Qdefault = XrmPermStringToQuark("default");
#endif

    /*
     * We don't need to check for null super since we'll get to TextSink
     * eventually.
     */
    if (t_src->text_sink_class.DisplayText == XtInheritDisplayText)
	t_src->text_sink_class.DisplayText =
	    superC->text_sink_class.DisplayText;

    if (t_src->text_sink_class.InsertCursor == XtInheritInsertCursor)
	t_src->text_sink_class.InsertCursor =
	    superC->text_sink_class.InsertCursor;

    if (t_src->text_sink_class.ClearToBackground== XtInheritClearToBackground)
	t_src->text_sink_class.ClearToBackground =
	    superC->text_sink_class.ClearToBackground;

    if (t_src->text_sink_class.FindPosition == XtInheritFindPosition)
	t_src->text_sink_class.FindPosition =
	    superC->text_sink_class.FindPosition;

    if (t_src->text_sink_class.FindDistance == XtInheritFindDistance)
	t_src->text_sink_class.FindDistance =
	    superC->text_sink_class.FindDistance;

    if (t_src->text_sink_class.Resolve == XtInheritResolve)
	t_src->text_sink_class.Resolve =
	    superC->text_sink_class.Resolve;

    if (t_src->text_sink_class.MaxLines == XtInheritMaxLines)
	t_src->text_sink_class.MaxLines =
	    superC->text_sink_class.MaxLines;

    if (t_src->text_sink_class.MaxHeight == XtInheritMaxHeight)
	t_src->text_sink_class.MaxHeight =
	    superC->text_sink_class.MaxHeight;

    if (t_src->text_sink_class.SetTabs == XtInheritSetTabs)
	t_src->text_sink_class.SetTabs =
	    superC->text_sink_class.SetTabs;

    if (t_src->text_sink_class.GetCursorBounds == XtInheritGetCursorBounds)
	t_src->text_sink_class.GetCursorBounds =
	    superC->text_sink_class.GetCursorBounds;

#ifndef OLDXAW
    XtSetTypeConverter(XtRString, XawRTextProperties, CvtStringToPropertyList,
		       &CvtArgs[0], XtNumber(CvtArgs), XtCacheNone, NULL);
    XtSetTypeConverter(XawRTextProperties, XtRString, CvtPropertyListToString,
		       NULL, 0, XtCacheNone, NULL);
#endif
}

/*
 * Function:
 *	XawTextSinkInitialize
 *
 * Parameters:
 *	request - requested and new values for the object instance
 *	cnew	- ""
 *
 * Description:
 *	Initializes the TextSink Object.
 */
/*ARGSUSED*/
static void
XawTextSinkInitialize(Widget request _X_UNUSED, Widget cnew,
		      ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TextSinkObject sink = (TextSinkObject)cnew;

    sink->text_sink.tab_count = 0; /* Initialize the tab stops. */
    sink->text_sink.tabs = NULL;
    sink->text_sink.char_tabs = NULL;
#ifndef OLDXAW
    sink->text_sink.paint = NULL;
#endif
}

/*
 * Function:
 *	XawTextSinkDestroy
 *
 * Parameters:
 *	w - TextSink Object
 *
 * Description:
 *	This function cleans up when the object is destroyed.
 */
static void
XawTextSinkDestroy(Widget w)
{
    TextSinkObject sink = (TextSinkObject) w;

    XtFree((char *)sink->text_sink.tabs);
    XtFree((char *)sink->text_sink.char_tabs);
}

/*
 * Function:
 *	XawTextSinkSetValues
 *
 * Parameters:
 *	current - current state of the object
 *	request - what was requested
 *	cnew	- what the object will become
 *
 * Description:
 *	Sets the values for the TextSink.
 *
 * Returns:
 *	True if redisplay is needed
 */
/*ARGSUSED*/
static Boolean
XawTextSinkSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		     ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TextSinkObject w = (TextSinkObject)cnew;
    TextSinkObject old_w = (TextSinkObject)current;

    if (w->text_sink.foreground != old_w->text_sink.foreground)
	((TextWidget)XtParent(cnew))->text.redisplay_needed = True;

    return (False);
}

/*
 * Function:
 *	DisplayText
 *
 * Parameters:
 *	w	  - TextSink Object
 *	x	  - location to start drawing text
 *	y	  - ""
 *	pos1	  - location of starting and ending points in the text buffer
 *	pos2	  - ""
 *		 highlight - highlight this text?
 *
 * Description:
 *	Stub function that in subclasses will display text.
 */
/*ARGSUSED*/
static void
DisplayText(Widget w _X_UNUSED, int x _X_UNUSED, int y _X_UNUSED,
	    XawTextPosition pos1 _X_UNUSED, XawTextPosition pos2 _X_UNUSED, Bool highlight _X_UNUSED)
{
    return;
}

/*
 * Function:
 *	InsertCursor
 *
 * Parameters:
 *	w     - TextSink Object
 *	x     - location for the cursor
 *	y     - ""
 *	state - whether to turn the cursor on, or off
 *
 * Description:
 *	Places the InsertCursor.
 */
/*ARGSUSED*/
static void
InsertCursor(Widget w _X_UNUSED, int x _X_UNUSED, int y _X_UNUSED, XawTextInsertState state _X_UNUSED)
{
    return;
}

/*
 * Function:
 *	ClearToBackground
 *
 * Parameters:
 *	w      - TextSink Object
 *	x      - location of area to clear
 *	y      - ""
 *	width  - size of area to clear
 *	height - ""
 *
 * Description:
 *	Clears a region of the sink to the background color.
 */
/*ARGSUSED*/
static void
ClearToBackground(Widget w, int x, int y,
		  unsigned int width, unsigned int height)
{
    /*
     * Don't clear in height or width are zero
     * XClearArea() has special semantic for these values
     */
    TextWidget xaw = (TextWidget)XtParent(w);
    Position x1, y1, x2, y2;

    x1 = (XawMax(x, xaw->text.r_margin.left));
    y1 = (XawMax(y, xaw->text.r_margin.top));
    x2 = (XawMin(x + (int)width, (int)XtWidth(xaw) - xaw->text.r_margin.right));
    y2 = (XawMin(y + (int)height, (int)XtHeight(xaw) - xaw->text.r_margin.bottom));

    x = (int)x1;
    y = (int)y1;
    width = (unsigned)(XawMax(0, x2 - x1));
    height = (unsigned)(XawMax(0, y2 - y1));

    if (height != 0 && width != 0)
	XClearArea(XtDisplayOfObject(w), XtWindowOfObject(w),
		   x, y, width, height, False);
}

/*
 * Function:
 *	FindPosition
 *
 * Parameters:
 *	w		- TextSink Object
 *	fromPos		- reference position
 *	fromX		- reference location
 *	width		- width of section to paint text
 *	stopAtWordBreak - returned position is a word break?
 *	resPos		- position found (return)
 *	resWidth	- Width actually used (return)
 *	resHeight	- Height actually used (return)
 *
 * Description:
 *	Finds a position in the text.
 */
/*ARGSUSED*/
static void
FindPosition(Widget w _X_UNUSED, XawTextPosition fromPos _X_UNUSED, int fromx _X_UNUSED, int width _X_UNUSED,
	     Bool stopAtWordBreak _X_UNUSED, XawTextPosition *resPos,
	     int *resWidth, int *resHeight)
{
    *resPos = fromPos;
    *resHeight = *resWidth = 0;
}

/*
 * Function:
 *	FindDistance
 *
 * Parameters:
 *	w	  - TextSink Object
 *	fromPos	  - starting Position
 *	fromX	  - x location of starting Position
 *	toPos	  - end Position
 *	resWidth  - Distance between fromPos and toPos
 *	resPos	  - Actual toPos used
 *	resHeight - Height required by this text
 *
 * Description:
 *	Find the Pixel Distance between two text Positions.
 */
/*ARGSUSED*/
static void
FindDistance(Widget w _X_UNUSED, XawTextPosition fromPos, int fromx _X_UNUSED,
	     XawTextPosition toPos _X_UNUSED, int *resWidth,
	     XawTextPosition *resPos, int *resHeight)
{
    *resWidth = *resHeight = 0;
    *resPos = fromPos;
}

/*
 * Function:
 *	Resolve
 *
 * Parameters:
 *	w      - TextSink Object
 *	pos    - reference Position
 *	fromx  - reference Location
 *	width  - width to move
 *	resPos - resulting position
 *
 * Description:
 *	Resolves a location to a position.
 */
/*ARGSUSED*/
static void
Resolve(Widget w _X_UNUSED, XawTextPosition pos _X_UNUSED, int fromx _X_UNUSED, int width _X_UNUSED,
	XawTextPosition *resPos)
{
    *resPos = pos;
}

/*
 * Function:
 *	MaxLines
 *
 * Parameters:
 *	w      - TextSink Object
 *	height - height to fit lines into
 *
 * Description:
 *	Finds the Maximum number of lines that will fit in a given height.
 *
 * Returns:
 *	Number of lines that will fit
 */
/*ARGSUSED*/
static int
MaxLines(Widget w _X_UNUSED, unsigned int height _X_UNUSED)
{
    /*
     * The fontset has gone down to descent Sink Widget, so
     * the functions such MaxLines, SetTabs... are bound to the descent.
     *
     * by Li Yuhong, Jan. 15, 1991
     */
    return (0);
}

/*
 * Function:
 *	MaxHeight
 *
 * Parameters:
 *	w     - TextSink Object
 *	lines - number of lines
 *
 * Description:
 *	Finds the Minimum height that will contain a given number lines.
 *
 * Returns:
 *	the height
 */
/*ARGSUSED*/
static int
MaxHeight(Widget w _X_UNUSED, int lines _X_UNUSED)
{
    return (0);
}

/*
 * Function:
 *	SetTabs
 *
 * Parameters:
 *	w	  - TextSink Object
 *	tab_count - the number of tabs in the list
 *	tabs	  - text positions of the tabs
 * Description:
 *	Sets the Tab stops.
 */
/*ARGSUSED*/
static void
SetTabs(Widget w _X_UNUSED, int tab_count _X_UNUSED, short *tabs _X_UNUSED)
{
    return;
}

/*
 * Function:
 *	GetCursorBounds
 *
 * Parameters:
 *	w - TextSinkObject.
 *	rect - X rectangle containing the cursor bounds
 *
 * Description:
 *	Finds the bounding box for the insert cursor (caret)
 */
/*ARGSUSED*/
static void
GetCursorBounds(Widget w _X_UNUSED, XRectangle *rect)
{
    rect->x = rect->y = (short)(rect->width = rect->height = 0);
}

/*
 * Public Functions
 */
/*
 * Function:
 *	XawTextSinkDisplayText
 *
 * Parameters:
 *	w	  - TextSink Object
 *	x	  - location to start drawing text
 *	y	  - ""
 *	pos1	  - location of starting and ending points in the text buffer
 *	pos2	  - ""
 *	highlight - highlight this text?
 */
/*ARGSUSED*/
void
XawTextSinkDisplayText(Widget w,
#if NeedWidePrototypes
		       int x, int y,
#else
		       Position x, Position y,
#endif
		       XawTextPosition pos1, XawTextPosition pos2,
#if NeedWidePrototypes
		       int highlight
#else
		       Boolean highlight
#endif
)
{
    _XawTextSinkDisplayText(w, x, y, pos1, pos2, highlight);
}

void
_XawTextSinkDisplayText(Widget w, int x, int y,
			XawTextPosition pos1, XawTextPosition pos2,
			Bool highlight)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.DisplayText)(w, x, y, pos1, pos2, highlight);
}

/*
 * Function:
 *	XawTextSinkInsertCursor
 *
 * Parameters:
 *	w     - TextSink Object
 *	x     - location for the cursor
 *	y     - ""
 *	state - whether to turn the cursor on, or off
 *
 * Description:
 *	Places the InsertCursor.
 */
/*ARGSUSED*/
void
#if NeedWidePrototypes
XawTextSinkInsertCursor(Widget w, int x, int y, int state)
#else
XawTextSinkInsertCursor(Widget w, Position x, Position y, XawTextInsertState state)
#endif
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.InsertCursor)(w, x, y, state);
}

/*
 * Function:
 *	XawTextSinkClearToBackground
 *
 * Parameters:
 *	w      - TextSink Object
 *	x      - location of area to clear
 *	y      - ""
 *	width  - size of area to clear
 *	height - ""
 *
 * Description:
 *	Clears a region of the sink to the background color.
 */
/*ARGSUSED*/
void
XawTextSinkClearToBackground(Widget w,
#if NeedWidePrototypes
			     int x, int y,
			     unsigned int width, unsigned int height
#else
			     Position x, Position y,
			     Dimension width, Dimension height
#endif
)
{
    _XawTextSinkClearToBackground(w, x, y, width, height);
}

void
_XawTextSinkClearToBackground(Widget w,
			      int x, int y,
			      unsigned int width, unsigned int height)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.ClearToBackground)(w, x, y, width, height);
}

/*
 * Function:
 *	XawTextSinkFindPosition
 *
 *  Parameters:
 *	w		- TextSink Object
 *	fromPos		- reference position
 *	fromX		- reference location
 *	width		- width of section to paint text
 *	stopAtWordBreak - returned position is a word break?
 *	resPos		- position found (return)
 *	resWidth	- Width actually used (return)
 *	resHeight	- Height actually used (return)
 *
 * Description:
 *	Finds a position in the text.
 */
/*ARGSUSED*/
void
XawTextSinkFindPosition(Widget w, XawTextPosition fromPos, int fromx, int width,
#if NeedWidePrototypes
			int stopAtWordBreak,
#else
			Boolean stopAtWordBreak,
#endif
			XawTextPosition *resPos, int *resWidth, int *resHeight)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.FindPosition)(w, fromPos, fromx, width,
					    stopAtWordBreak,
					    resPos, resWidth, resHeight);
}

/*
 * Function:
 *	XawTextSinkFindDistance
 *
 *  Parameters:
 *	w	  - TextSink Object
 *	fromPos	  - starting Position
 *	fromX	  - x location of starting Position
 *	toPos	  - end Position
 *	resWidth  - Distance between fromPos and toPos
 *	resPos	  - Actual toPos used
 *	resHeight - Height required by this text
 *
 * Description:
 *	Find the Pixel Distance between two text Positions.
 */
/*ARGSUSED*/
void
XawTextSinkFindDistance(Widget w, XawTextPosition fromPos, int fromx,
			XawTextPosition toPos, int *resWidth,
			XawTextPosition *resPos, int *resHeight)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.FindDistance)(w, fromPos, fromx, toPos,
					    resWidth, resPos, resHeight);
}

/*
 * Function:
 *	XawTextSinkResolve
 *
 *  Parameters:
 *	w      - TextSink Object
 *	pos    - reference Position
 *	fromx  - reference Location
 *	width  - width to move
 *	resPos - resulting position
 *
 * Description:
 *	Resolves a location to a position.
 */
/*ARGSUSED*/
void
XawTextSinkResolve(Widget w, XawTextPosition pos, int fromx, int width,
		   XawTextPosition *resPos)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass) w->core.widget_class;

    (*cclass->text_sink_class.Resolve)(w, pos, fromx, width, resPos);
}

/*
 * Function:
 *	XawTextSinkMaxLines
 *
 *  Parameters:
 *	w      - TextSink Object
 *	height - height to fit lines into
 *
 * Description:
 *	Finds the Maximum number of lines that will fit in a given height.
 *
 * Returns:
 *	Number of lines that will fit
 */
/*ARGSUSED*/
int
#if NeedWidePrototypes
XawTextSinkMaxLines(Widget w, unsigned int height)
#else
XawTextSinkMaxLines(Widget w, Dimension height)
#endif
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    return((*cclass->text_sink_class.MaxLines)(w, height));
}

/*
 * Function:
 *	XawTextSinkMaxHeight
 *
 *  Parameters:
 *	w     - TextSink Object
 *	lines - number of lines
 *
 * Description:
 *	Finds the Minimum height that will contain a given number lines.
 *
 * Returns:
 *	the height
 */
/*ARGSUSED*/
int
XawTextSinkMaxHeight(Widget w, int lines)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    return((*cclass->text_sink_class.MaxHeight)(w, lines));
}

/*
 * Function:
 *	XawTextSinkSetTabs
 *
 *  Parameters:
 *	w	  - TextSink Object
 *	tab_count - the number of tabs in the list
 *	tabs	  - text positions of the tabs
 * Description:
 *	Sets the Tab stops.
 */
void
XawTextSinkSetTabs(Widget w, int tab_count, int *tabs)
{
    if (tab_count > 0) {
	TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;
	short *char_tabs = (short*)XtMalloc((Cardinal)((unsigned)tab_count * sizeof(short)));
	short *tab, len = 0;
	int i;

	for (i = tab_count, tab = char_tabs; i; i--) {
	    if ((short)*tabs > len)
		*tab++ = (len = (short)*tabs++);
	    else {
		tabs++;
		--tab_count;
	    }
	}

	if (tab_count > 0)
	    (*cclass->text_sink_class.SetTabs)(w, tab_count, char_tabs);
	XtFree((char *)char_tabs);
    }
}

/*
 * Function:
 *	XawTextSinkGetCursorBounds
 *
 * Parameters:
 *	w    - TextSinkObject
 *	rect - X rectance containing the cursor bounds
 *
 * Description:
 *	Finds the bounding box for the insert cursor (caret).
 */
/*ARGSUSED*/
void
XawTextSinkGetCursorBounds(Widget w, XRectangle *rect)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.GetCursorBounds)(w, rect);
}

#ifndef OLDXAW
Bool
XawTextSinkBeginPaint(Widget w)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    if (cclass->text_sink_class.extension->BeginPaint == NULL ||
	cclass->text_sink_class.extension->PreparePaint == NULL ||
	cclass->text_sink_class.extension->DoPaint == NULL ||
	cclass->text_sink_class.extension->EndPaint == NULL)
	return (False);

    return ((*cclass->text_sink_class.extension->BeginPaint)(w));
}

static Bool
BeginPaint(Widget w)
{
    TextSinkObject sink = (TextSinkObject)w;

    if (sink->text_sink.paint != NULL)
	return (False);

    sink->text_sink.paint = XtNew(XawTextPaintList);
    sink->text_sink.paint->clip = XmuCreateArea();
    sink->text_sink.paint->hightabs = NULL;
    sink->text_sink.paint->paint = NULL;
    sink->text_sink.paint->bearings = NULL;

    return (True);
}

void
XawTextSinkPreparePaint(Widget w, int y, int line, XawTextPosition from,
			XawTextPosition to, Bool highlight)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.extension->PreparePaint)
	(w, y, line, from, to, highlight);
}

#if 0
/*ARGSUSED*/
static void
PreparePaint(Widget w, int y, int line, XawTextPosition from, XawTextPosition to,
	     Bool highlight)
{
}
#endif

void
XawTextSinkDoPaint(Widget w)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    (*cclass->text_sink_class.extension->DoPaint)(w);
}

#if 0
/*ARGSUSED*/
static void
DoPaint(Widget w)
{
}
#endif

Bool
XawTextSinkEndPaint(Widget w)
{
    TextSinkObjectClass cclass = (TextSinkObjectClass)w->core.widget_class;

    return ((*cclass->text_sink_class.extension->EndPaint)(w));
}

static Bool
EndPaint(Widget w)
{
    TextSinkObject sink = (TextSinkObject)w;
    XawTextPaintStruct *paint, *next;

    if (sink->text_sink.paint == NULL)
	return (False);

    XmuDestroyArea(sink->text_sink.paint->clip);
    if (sink->text_sink.paint->hightabs)
	XmuDestroyArea(sink->text_sink.paint->hightabs);
    paint = sink->text_sink.paint->paint;
    while (paint) {
	next = paint->next;
	if (paint->text)
	    XtFree((XtPointer)paint->text);
	if (paint->backtabs)
	    XmuDestroyArea(paint->backtabs);
	XtFree((XtPointer)paint);
	paint = next;
    }

    paint = sink->text_sink.paint->bearings;
    while (paint) {
	next = paint->next;
	if (paint->text)
	    XtFree((XtPointer)paint->text);
	XtFree((XtPointer)paint);
	paint = next;
    }

    XtFree((XtPointer)sink->text_sink.paint);
    sink->text_sink.paint = NULL;
    return (True);
}

static XawTextPropertyList **prop_lists;
static Cardinal num_prop_lists;

static int
bcmp_qident(_Xconst void *left, _Xconst void *right)
{
    return (int)((long)left - (*(XawTextProperty**)right)->identifier);
}

static int
qcmp_qident(_Xconst void *left, _Xconst void *right)
{
    return ((*(XawTextProperty**)left)->identifier -
	    (*(XawTextProperty**)right)->identifier);
}

static void
SetXlfdDefaults(Display *display, XawTextProperty *property)
{
    Atom atom = XInternAtom(display, "FONT", True);
    unsigned long value;
    char *str;

    if (XGetFontProperty(property->font, atom, &value)) {
	char *xlfd = XGetAtomName(display, value);

	if (xlfd) {
	    char *sep = xlfd + 1;
	    char *name = sep;

	    property->xlfd = XrmStringToQuark(xlfd);

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->foundry = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');	*sep++ = '\0';
	    property->family = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');	*sep++ = '\0';
	    property->weight = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');	*sep++ = '\0';
	    property->slant = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');	*sep++ = '\0';
	    property->setwidth = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->addstyle = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->pixel_size = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->point_size = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->res_x = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->res_y = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->spacing = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->avgwidth = XrmStringToQuark(name);
	    name = sep;

	    sep = strchr(sep, '-');     *sep++ = '\0';
	    property->registry = XrmStringToQuark(name);
	    name = sep;

	    property->encoding = XrmStringToQuark(name);

	    XFree(xlfd);
	}
    }

    atom = XInternAtom(display, "UNDERLINE_THICKNESS", True);
    if (XGetFontProperty(property->font, atom, &value) &&
	(str = XGetAtomName(display, value)) != NULL) {
	property->underline_thickness = (short)(atoi(str));
	XFree(str);
    }
    else {
	/* XLFD says:
	 * CapStemWidth = average width of the stems of capitals
	 * if (UNDERLINE_THICKNESS undefined) then
	 *   UNDERLINE_THICKNESS = CapStemWidth
	 *
	 * How do I know the value of CapStemWidth??
	 */
	if (property->pixel_size != NULLQUARK) {
	    property->underline_thickness =
		(short)(atoi(XrmQuarkToString(property->pixel_size)) / 10);
	    property->underline_thickness =
		(XawMax(1, property->underline_thickness));
	}
	else
	    property->underline_thickness = 1;
    }

    atom = XInternAtom(display, "UNDERLINE_POSITION", True);
    if (XGetFontProperty(property->font, atom, &value) &&
	(str = XGetAtomName(display, value)) != NULL) {
	property->underline_position = (short)(atoi(str));
	XFree(str);
    }
    else
	/* XLFD says:
	 * if (UNDERLINE_POSITION undefined) then
	 *   UNDERLINE_POSITION = ROUND((maximum_descent) / 2)
	 */
	property->underline_position =
	    property->font->max_bounds.descent >> 1;

    /* I am assuming xlfd does not consider that lines are
     * centered in the path */
    property->underline_position = (short)(property->underline_position
					   + (property->underline_thickness >> 1));
}

static void
DestroyTextPropertyList(XawTextPropertyList *list)
{
    int i;

    for (i = 0; (Cardinal)i < list->num_properties; i++) {
	if (list->properties[i]->font)
	    XFreeFont(DisplayOfScreen(list->screen), list->properties[i]->font);
	XtFree((char*)list->properties[i]);
    }
    if (list->properties)
	XtFree((char*)list->properties);
    XtFree((char*)list);
}

static XawTextProperty *
_XawTextSinkGetProperty(XawTextPropertyList *list, XrmQuark property)
{
    if (property != NULLQUARK && list && list->properties) {
	XawTextProperty **ptr = (XawTextProperty**)
	    bsearch((void*)(long)property,
		    list->properties, list->num_properties,
		    sizeof(XawTextProperty*), bcmp_qident);

	if (ptr)
	    return (*ptr);
    }

    return (NULL);
}

XawTextProperty *
XawTextSinkGetProperty(Widget w, XrmQuark property)
{
    TextSinkObject sink = (TextSinkObject)w;
    XawTextPropertyList *list = sink->text_sink.properties;

    return (_XawTextSinkGetProperty(list, property));
}

XawTextProperty *
XawTextSinkCopyProperty(Widget w, XrmQuark property)
{
    XawTextProperty *cur, *ret;

    if ((cur = XawTextSinkGetProperty(w, property)) == NULL)
	cur = XawTextSinkGetProperty(w, Qdefault);
    ret = (XawTextProperty*)XtCalloc(1, sizeof(XawTextProperty));
    if (cur)
	memcpy(ret, cur, sizeof(XawTextProperty));
    ret->identifier = NULLQUARK;
    ret->mask &= (unsigned long)(~XAW_TPROP_FONT);

    return (ret);
}

static XawTextProperty *
_XawTextSinkAddProperty(XawTextPropertyList *list, XawTextProperty *property,
			Bool replace)
{
    XawTextProperty *result;
    XColor color;
    char identifier[1024];
    char foreground[16];
    char background[16];
    const char *foundry, *family, *weight, *slant, *setwidth, *addstyle, *pixel_size,
	 *point_size, *res_x, *res_y, *spacing, *avgwidth, *registry, *encoding;
    const char *xlfd;
    static const char *asterisk = "*";
    static const char *null = "";
    XrmQuark quark;

    if (list == NULL || property == NULL)
	return (NULL);

    if (property->mask & XAW_TPROP_FOREGROUND) {
	color.pixel = property->foreground;
	XQueryColor(DisplayOfScreen(list->screen), list->colormap, &color);
	snprintf(foreground, sizeof(foreground), "%04x%04x%04x",
		 color.red, color.green, color.blue);
    }
    else
	strcpy(foreground, asterisk);
    if (property->mask & XAW_TPROP_BACKGROUND) {
	color.pixel = property->background;
	XQueryColor(DisplayOfScreen(list->screen), list->colormap, &color);
	snprintf(background, sizeof(background), "%04x%04x%04x",
		 color.red, color.green, color.blue);
    }
    else
	strcpy(background, asterisk);

    if (property->xlfd_mask & XAW_TPROP_FOUNDRY)
	foundry = XrmQuarkToString(property->foundry);
    else
	foundry = asterisk;

    /* use default, or what was requested */
    if (property->family != NULLQUARK)
	family = XrmQuarkToString(property->family);
    else
	family = asterisk;
    if (property->weight != NULLQUARK)
	weight = XrmQuarkToString(property->weight);
    else
	weight = asterisk;
    if (property->slant != NULLQUARK) {
	slant = XrmQuarkToString(property->slant);
	if (toupper((unsigned char)*slant) != 'R')
	    slant = asterisk;	/* X defaults to italics, so, don't
				   care in resolving between `I' and `O' */
    }
    else
	slant = asterisk;

    if (property->xlfd_mask & XAW_TPROP_SETWIDTH)
	setwidth = XrmQuarkToString(property->setwidth);
    else
	setwidth = asterisk;
    if (property->xlfd_mask & XAW_TPROP_ADDSTYLE)
	addstyle = XrmQuarkToString(property->addstyle);
    else
	addstyle = null;

    /* use default, or what was requested */
    if (!(property->mask & XAW_TPROP_POINTSIZE) &&
	property->pixel_size != NULLQUARK)
	pixel_size = XrmQuarkToString(property->pixel_size);
    else
	pixel_size = asterisk;

    if (property->xlfd_mask & XAW_TPROP_POINTSIZE)
	point_size = XrmQuarkToString(property->point_size);
    else
	point_size = asterisk;
    if (property->xlfd_mask & XAW_TPROP_RESX)
	res_x = XrmQuarkToString(property->res_x);
    else
	res_x = asterisk;
    if (property->xlfd_mask & XAW_TPROP_RESY)
	res_y = XrmQuarkToString(property->res_y);
    else
	res_y = asterisk;
    if (property->xlfd_mask & XAW_TPROP_SPACING)
	spacing = XrmQuarkToString(property->spacing);
    else
	spacing = asterisk;
    if (property->xlfd_mask & XAW_TPROP_AVGWIDTH)
	avgwidth = XrmQuarkToString(property->avgwidth);
    else
	avgwidth = asterisk;

    /* use default, or what that was requested */
    if (property->registry != NULLQUARK)
	registry = XrmQuarkToString(property->registry);
    else
	registry = asterisk;
    if (property->encoding != NULLQUARK)
	encoding = XrmQuarkToString(property->encoding);
    else
	encoding = asterisk;

    if (replace) {
	result = XtNew(XawTextProperty);
	memcpy(result, property, sizeof(XawTextProperty));
    }
    else
	result = property;

    /* XXX should do the best to load a suitable font here */
    if (!(result->mask & XAW_TPROP_FONT)) {
	snprintf(identifier, sizeof(identifier),
		 "-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s",
		 foundry, family, weight, slant, setwidth, addstyle,
		 pixel_size, point_size, res_x, res_y, spacing, avgwidth,
		 registry, encoding);
	if ((result->font = XLoadQueryFont(DisplayOfScreen(list->screen),
					   identifier)) != NULL) {
	    result->mask |= XAW_TPROP_FONT;
	    SetXlfdDefaults(DisplayOfScreen(list->screen), result);
	}
	else
	    result->mask &= (unsigned long)(~XAW_TPROP_FONT);
    }

    if (result->font)
	xlfd = XrmQuarkToString(result->xlfd);
    else
	xlfd = null;

    snprintf(identifier, sizeof(identifier), "%08lx%08lx%s%s%d%d%d%d%s",
	     property->mask, property->xlfd_mask,
	     foreground, background,
	     (result->mask & XAW_TPROP_UNDERLINE) != 0,
	     (result->mask & XAW_TPROP_OVERSTRIKE) != 0,
	     (result->mask & XAW_TPROP_SUBSCRIPT) != 0,
	     (result->mask & XAW_TPROP_SUPERSCRIPT) != 0,
	     xlfd);

    quark = XrmStringToQuark(identifier);
    if (result->identifier == NULLQUARK)
	result->identifier = quark;
    result->code = quark;

    if ((property = _XawTextSinkGetProperty(list, result->identifier)) != NULL) {
	if (result->font)
	    XFreeFont(DisplayOfScreen(list->screen), result->font);
	if (replace)
	    XtFree((XtPointer)result);

	return (property);
    }

    list->properties = (XawTextProperty**)
	XtRealloc((XtPointer)list->properties,
		  (Cardinal)(sizeof(XawTextProperty*) *
			     (list->num_properties + 1)));
    list->properties[list->num_properties++] = result;
    qsort((void*)list->properties, list->num_properties,
	      sizeof(XawTextProperty*), qcmp_qident);

    return (result);
}

XawTextProperty *
XawTextSinkAddProperty(Widget w, XawTextProperty *property)
{
    TextSinkObject sink = (TextSinkObject)w;
    XawTextPropertyList *list = sink->text_sink.properties;

    return (_XawTextSinkAddProperty(list, property, True));
}

XawTextProperty *
XawTextSinkCombineProperty(Widget w _X_UNUSED,
			   XawTextProperty *property, XawTextProperty *combine,
			   Bool override)
{
    if (property == NULL || combine == NULL)
	return (property);

    if ((override || !(property->mask & XAW_TPROP_FOREGROUND)) &&
	(combine->mask & XAW_TPROP_FOREGROUND)) {
	property->mask |= XAW_TPROP_FOREGROUND;
	property->foreground = combine->foreground;
    }
    if ((override || !(property->mask & XAW_TPROP_BACKGROUND)) &&
	(combine->mask & XAW_TPROP_BACKGROUND)) {
	property->mask |= XAW_TPROP_BACKGROUND;
	property->background = combine->background;
    }
    if ((override || !(property->mask & XAW_TPROP_FPIXMAP)) &&
	(combine->mask & XAW_TPROP_FPIXMAP)) {
	property->mask |= XAW_TPROP_FPIXMAP;
	property->foreground_pixmap = combine->foreground_pixmap;
    }
    if ((override || !(property->mask & XAW_TPROP_BPIXMAP)) &&
	(combine->mask & XAW_TPROP_BPIXMAP)) {
	property->mask |= XAW_TPROP_BPIXMAP;
	property->background_pixmap = combine->background_pixmap;
    }
    if (combine->mask & XAW_TPROP_UNDERLINE)
	property->mask |= XAW_TPROP_UNDERLINE;
    if (combine->mask & XAW_TPROP_OVERSTRIKE)
	property->mask |= XAW_TPROP_OVERSTRIKE;
    if ((override || !(property->mask & XAW_TPROP_SUPERSCRIPT)) &&
	(combine->mask & XAW_TPROP_SUBSCRIPT))
	property->mask |= XAW_TPROP_SUBSCRIPT;
    if ((property->mask & XAW_TPROP_SUBSCRIPT) &&
	(combine->mask & XAW_TPROP_SUPERSCRIPT))
	property->mask |= XAW_TPROP_SUPERSCRIPT;
    if ((override || !(property->xlfd_mask & XAW_TPROP_FOUNDRY)) &&
	(combine->xlfd_mask & XAW_TPROP_FOUNDRY)) {
	property->xlfd_mask |= XAW_TPROP_FOUNDRY;
	property->foundry = combine->foundry;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_FAMILY)) &&
	(combine->xlfd_mask & XAW_TPROP_FAMILY)) {
	property->xlfd_mask |= XAW_TPROP_FAMILY;
	property->family = combine->family;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_WEIGHT)) &&
	(combine->xlfd_mask & XAW_TPROP_WEIGHT)) {
	property->xlfd_mask |= XAW_TPROP_WEIGHT;
	property->weight = combine->weight;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_SLANT)) &&
	(combine->xlfd_mask & XAW_TPROP_SLANT)) {
	property->xlfd_mask |= XAW_TPROP_SLANT;
	property->slant = combine->slant;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_SETWIDTH)) &&
	(combine->xlfd_mask & XAW_TPROP_SETWIDTH)) {
	property->xlfd_mask |= XAW_TPROP_SETWIDTH;
	property->setwidth = combine->setwidth;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_ADDSTYLE)) &&
	(combine->xlfd_mask & XAW_TPROP_ADDSTYLE)) {
	property->xlfd_mask |= XAW_TPROP_ADDSTYLE;
	property->addstyle = combine->addstyle;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_PIXELSIZE)) &&
	(combine->xlfd_mask & XAW_TPROP_PIXELSIZE)) {
	property->xlfd_mask |= XAW_TPROP_PIXELSIZE;
	property->pixel_size = combine->pixel_size;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_POINTSIZE)) &&
	(combine->xlfd_mask & XAW_TPROP_POINTSIZE)) {
	property->xlfd_mask |= XAW_TPROP_POINTSIZE;
	property->point_size = combine->point_size;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_RESX)) &&
	(combine->xlfd_mask & XAW_TPROP_RESX)) {
	property->xlfd_mask |= XAW_TPROP_RESX;
	property->res_x = combine->res_x;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_RESY)) &&
	(combine->xlfd_mask & XAW_TPROP_RESY)) {
	property->xlfd_mask |= XAW_TPROP_RESY;
	property->res_y = combine->res_y;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_SPACING)) &&
	(combine->xlfd_mask & XAW_TPROP_SPACING)) {
	property->xlfd_mask |= XAW_TPROP_SPACING;
	property->spacing = combine->spacing;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_AVGWIDTH)) &&
	(combine->xlfd_mask & XAW_TPROP_AVGWIDTH)) {
	property->xlfd_mask |= XAW_TPROP_AVGWIDTH;
	property->avgwidth = combine->avgwidth;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_REGISTRY)) &&
	(combine->xlfd_mask & XAW_TPROP_REGISTRY)) {
	property->xlfd_mask |= XAW_TPROP_REGISTRY;
	property->registry = combine->registry;
    }
    if ((override || !(property->xlfd_mask & XAW_TPROP_ENCODING)) &&
	(combine->xlfd_mask & XAW_TPROP_ENCODING)) {
	property->xlfd_mask |= XAW_TPROP_ENCODING;
	property->encoding = combine->encoding;
    }

    return (property);
}

/*
 * The default property must be defined first, if the code is willing to
 * combine properties.
 */
XawTextPropertyList *
XawTextSinkConvertPropertyList(String name, String spec, Screen *screen,
			       Colormap colormap, int depth)
{
    XrmQuark qname = XrmStringToQuark(name);
    XawTextPropertyList **ptr = NULL;
    XawTextPropertyList *propl, *prev = NULL;
    XawTextProperty *def_prop = NULL;
    char * str;
    String tok;
    char *tmp;
    char buffer[BUFSIZ];

    if (prop_lists) ptr = (XawTextPropertyList**)
	bsearch((void*)(long)qname, prop_lists, num_prop_lists,
		sizeof(XawTextPropertyList*), bcmp_qident);
    if (ptr) {
	propl = *ptr;
	while (propl) {
	    prev = propl;
	    if (propl->screen == screen &&
		propl->colormap == colormap &&
		propl->depth == depth)
		return (propl);
	    propl = propl->next;
	}
    }

    propl = XtNew(XawTextPropertyList);
    propl->identifier = qname;
    propl->screen = screen;
    propl->colormap = colormap;
    propl->depth = depth;
    propl->next = NULL;

    if (prev)
	prev->next = propl;

    propl->properties = NULL;
    propl->num_properties = 0;

    str = XtNewString(spec);
    for (tok = str; tok; tok = tmp) {
	XawTextProperty *prop;
	XawParams *params;
	XrmQuark ident;
	XawArgVal *argval;
	XColor color, exact;

	if (def_prop == NULL && propl->num_properties)
	    def_prop = _XawTextSinkGetProperty(propl, Qdefault);
	tmp = strchr(tok, ',');
	if (tmp) {
	    *tmp = '\0';
	    if (*++tmp == '\0')
		tmp = NULL;
	}
	params = XawParseParamsString(tok);
	ident = XrmStringToQuark(params->name);
	if (ident == NULLQUARK) {
	    snprintf(buffer, sizeof(buffer), "Bad text property name \"%s\".",
		     params->name);
	    XtAppWarning(XtDisplayToApplicationContext
			 (DisplayOfScreen(screen)), buffer);
	    DestroyTextPropertyList(propl);
	    if (prev)
		prev->next = NULL;
	    XawFreeParamsStruct(params);
	    return (NULL);
	}
	else if (_XawTextSinkGetProperty(propl, ident) != NULL) {
	    XawFreeParamsStruct(params);
	    continue;
	}

	prop = (XawTextProperty*)XtCalloc(1, sizeof(XawTextProperty));
	prop->identifier = ident;

	if ((argval = XawFindArgVal(params, "font")) != NULL &&
	    argval->value) {

	    if ((prop->font = XLoadQueryFont(DisplayOfScreen(screen),
					     argval->value)) == NULL) {
		snprintf(buffer, sizeof(buffer), "Cannot load font \"%s\".",
			 argval->value);
		XtAppWarning(XtDisplayToApplicationContext
			     (DisplayOfScreen(screen)), buffer);
		DestroyTextPropertyList(propl);
		if (prev)
		    prev->next = NULL;
		XawFreeParamsStruct(params);
		XtFree((char *)prop);
		return (NULL);
	    }
	    prop->mask |= XAW_TPROP_FONT;
	    SetXlfdDefaults(DisplayOfScreen(screen), prop);
	}
	/* fontset processing here */

	if ((argval = XawFindArgVal(params, "foreground")) != NULL &&
	    argval->value) {
	    if (!XAllocNamedColor(DisplayOfScreen(screen), colormap,
				  argval->value, &color, &exact)) {
		snprintf(buffer, sizeof(buffer),
			 "Cannot allocate color \"%s\".", argval->value);
		XtAppWarning(XtDisplayToApplicationContext
			     (DisplayOfScreen(screen)), buffer);
		DestroyTextPropertyList(propl);
		if (prev)
		    prev->next = NULL;
		XawFreeParamsStruct(params);
		XtFree((char *)prop);
		return (NULL);
	    }
	    prop->foreground = color.pixel;
	    prop->mask |= XAW_TPROP_FOREGROUND;
	}
	if ((argval = XawFindArgVal(params, "background")) != NULL &&
	    argval->value) {
	    if (!XAllocNamedColor(DisplayOfScreen(screen), colormap,
				  argval->value, &color, &exact)) {
		snprintf(buffer, sizeof(buffer),
			 "Cannot allocate color \"%s\".", argval->value);
		XtAppWarning(XtDisplayToApplicationContext
			     (DisplayOfScreen(screen)), buffer);
		DestroyTextPropertyList(propl);
		if (prev)
		    prev->next = NULL;
		XawFreeParamsStruct(params);
		XtFree((char *)prop);
		return (NULL);
	    }
	    prop->background = color.pixel;
	    prop->mask |= XAW_TPROP_BACKGROUND;
	}
	/* foreground_pixmap and background_pixmap processing here */

	if (XawFindArgVal(params, "underline"))
	    prop->mask |= XAW_TPROP_UNDERLINE;
	if (XawFindArgVal(params, "overstrike"))
	    prop->mask |= XAW_TPROP_OVERSTRIKE;

	if (XawFindArgVal(params, "subscript"))
	    prop->mask |= XAW_TPROP_SUBSCRIPT;
	else if (XawFindArgVal(params, "superscript"))
	    prop->mask |= XAW_TPROP_SUPERSCRIPT;

	/* xlfd */
	if ((argval = XawFindArgVal(params, "foundry")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_FOUNDRY;
	    prop->foundry = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "family")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_FAMILY;
	    prop->family = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "weight")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_WEIGHT;
	    prop->weight = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "slant")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_SLANT;
	    prop->slant = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "setwidth")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_SETWIDTH;
	    prop->setwidth = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "addstyle")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_ADDSTYLE;
	    prop->addstyle = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "pixelsize")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_PIXELSIZE;
	    prop->pixel_size = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "pointsize")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_POINTSIZE;
	    prop->point_size = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "resx")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_RESX;
	    prop->res_x = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "resy")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_RESY;
	    prop->res_y = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "spacing")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_SPACING;
	    prop->spacing = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "avgwidth")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_AVGWIDTH;
	    prop->avgwidth = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "registry")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_REGISTRY;
	    prop->registry = XrmStringToQuark(argval->value);
	}
	if ((argval = XawFindArgVal(params, "encoding")) != NULL &&
	    argval->value) {
	    prop->xlfd_mask |= XAW_TPROP_ENCODING;
	    prop->encoding = XrmStringToQuark(argval->value);
	}

	if (def_prop)
	    (void)XawTextSinkCombineProperty(NULL, prop, def_prop, False);
	(void)_XawTextSinkAddProperty(propl, prop, False);

	XawFreeParamsStruct(params);
    }

    prop_lists = (XawTextPropertyList**)
    XtRealloc((XtPointer)prop_lists,
	      (Cardinal)(sizeof(XawTextPropertyList*) *
			 (num_prop_lists + 1)));
    prop_lists[num_prop_lists++] = propl;
    qsort((void*)prop_lists, num_prop_lists, sizeof(XawTextPropertyList*),
	  qcmp_qident);

    XtFree(str);

    return (propl);
}

/*ARGSUSED*/
static Boolean
CvtStringToPropertyList(Display *dpy, XrmValue *args, Cardinal *num_args,
			XrmValue *fromVal, XrmValue *toVal,
			XtPointer *converter_data _X_UNUSED)
{
    XawTextPropertyList *propl = NULL;
    String name;
    Widget w;

    if (*num_args != 1) {
      XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		      "wrongParameters", "cvtStringToTextProperties",
		      "ToolkitError",
		      "String to textProperties conversion needs widget argument",
		      NULL, NULL);
	return (False);
    }

    w = *(Widget*)args[0].addr;
    while (w && !XtIsWidget(w))
	w = XtParent(w);

    name = (String)(fromVal[0].addr);

    if (w) {
	XawTextPropertyList **ptr = NULL;
	if (prop_lists) ptr = (XawTextPropertyList**)
	    bsearch((void*)(long)XrmStringToQuark(name),
		    prop_lists, num_prop_lists,
		    sizeof(XawTextPropertyList*), bcmp_qident);

	if (ptr) {
	    Screen *screen = w->core.screen;
	    Colormap colormap = w->core.colormap;
	    int depth = (int)w->core.depth;

	    propl = *ptr;
	    while (propl) {
		if (propl->screen == screen &&
		    propl->colormap == colormap &&
		    propl->depth == depth)
		    break;
		propl = propl->next;
	    }
	}
    }

    if (!propl) {
	XtDisplayStringConversionWarning(dpy, (String)fromVal->addr,
					 XawRTextProperties);
	toVal->addr = NULL;
	toVal->size = sizeof(XawTextPropertyList*);
	return (False);
    }

    if (toVal->addr != NULL) {
	if (toVal->size < sizeof(XawTextPropertyList*))	{
	    toVal->size = sizeof(XawTextPropertyList*);
	    return (False);
	}
	*(XawTextPropertyList**)(toVal->addr) = propl;
    }
    else {
	static XawTextPropertyList *static_val;

	static_val = propl;
	toVal->addr = (XPointer)&static_val;
    }
    toVal->size = sizeof(XawTextProperty*);

    return (True);
}

/*ARGSUSED*/
static Boolean
CvtPropertyListToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args _X_UNUSED,
			XrmValue *fromVal, XrmValue *toVal,
			XtPointer *converter_data _X_UNUSED)
{
    static char *buffer;
    Cardinal size;
    XawTextPropertyList *propl;

    propl = *(XawTextPropertyList**)fromVal[0].addr;
    if (propl == NULL) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		      "wrongParameters", "cvtPropertyListToString",
		      "ToolkitError",
		      "textProperties to String conversion needs property list argument",
		      NULL, NULL);
	return (False);
    }

    buffer = XrmQuarkToString(propl->identifier);
    size = (Cardinal)(strlen(buffer) + 1);

    if (toVal->addr != NULL) {
	if (toVal->size < size) {
	    toVal->size = size;
	    return (False);
	}
	strcpy((char *)toVal->addr, buffer);
    }
    else
	toVal->addr = buffer;
    toVal->size = size;

    return (True);
}
#endif
