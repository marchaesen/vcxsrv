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
#include <X11/Xmu/Drawing.h>
#include <X11/Xaw/SimpleP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"
#ifndef OLDXAW
#include <X11/Xaw/Tip.h>
#endif

/*
 * Class Methods
 */
static Bool ChangeSensitive(Widget);
static void XawSimpleClassInitialize(void);
static void XawSimpleClassPartInitialize(WidgetClass);
#ifndef OLDXAW
static void XawSimpleInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawSimpleDestroy(Widget);
static void XawSimpleExpose(Widget, XEvent*, Region);
#endif
static void XawSimpleRealize(Widget, Mask*, XSetWindowAttributes*);
static Boolean XawSimpleSetValues(Widget, Widget, Widget, ArgList, Cardinal*);

/*
 * Prototypes
 */
static void ConvertCursor(Widget);

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

#define offset(field) XtOffsetOf(SimpleRec, simple.field)
static XtResource resources[] = {
  {
    XtNcursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(cursor),
    XtRImmediate,
    (XtPointer)None
  },
  {
    XtNinsensitiveBorder,
    XtCInsensitive,
    XtRPixmap,
    sizeof(Pixmap),
    offset(insensitive_border),
    XtRImmediate,
    NULL
  },
  {
    XtNpointerColor,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    offset(pointer_fg),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNpointerColorBackground,
    XtCBackground,
    XtRPixel,
    sizeof(Pixel),
    offset(pointer_bg),
    XtRString,
    (XtPointer)XtDefaultBackground
  },
  {
    XtNcursorName,
    XtCCursor,
    XtRString,
    sizeof(String),
    offset(cursor_name),
    XtRString,
    NULL
  },
  {
    XtNinternational,
    XtCInternational,
    XtRBoolean,
    sizeof(Boolean),
    offset(international),
    XtRImmediate,
    (XtPointer)False
  },
#ifndef OLDXAW
  {
    XawNdisplayList,
    XawCDisplayList,
    XawRDisplayList,
    sizeof(XawDisplayList*),
    offset(display_list),
    XtRImmediate,
    NULL
  },
  {
    XtNtip,
    XtCTip,
    XtRString,
    sizeof(String),
    offset(tip),
    XtRImmediate,
    NULL
  },
#endif
#undef offset
};

SimpleClassRec simpleClassRec = {
  /* core */
  {
    (WidgetClass)&widgetClassRec,	/* superclass */
    "Simple",				/* class_name */
    sizeof(SimpleRec),			/* widget_size */
    XawSimpleClassInitialize,		/* class_initialize */
    XawSimpleClassPartInitialize,	/* class_part_initialize */
    False,				/* class_inited */
#ifndef OLDXAW
    XawSimpleInitialize,		/* initialize */
#else
    NULL,				/* initialize */
#endif
    NULL,				/* initialize_hook */
    XawSimpleRealize,			/* realize */
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
#ifndef OLDXAW
    XawSimpleDestroy,			/* destroy */
#else
    NULL,				/* destroy */
#endif
    NULL,				/* resize */
#ifndef OLDXAW
    XawSimpleExpose,			/* expose */
#else
    NULL,				/* expose */
#endif
    XawSimpleSetValues,			/* set_values */
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
  /* simple */
  {
    ChangeSensitive,			/* change_sensitive */
  },
};

WidgetClass simpleWidgetClass = (WidgetClass)&simpleClassRec;

static void
XawSimpleClassInitialize(void)
{
    static XtConvertArgRec convertArg[] = {
    {
      XtWidgetBaseOffset,
      (XtPointer)XtOffsetOf(WidgetRec, core.screen),
      sizeof(Screen *)
    },
    {
      XtResourceString,
      (XtPointer)XtNpointerColor,
      sizeof(Pixel)
    },
    {
      XtResourceString,
      (XtPointer)XtNpointerColorBackground,
      sizeof(Pixel)
    },
    {
      XtWidgetBaseOffset,
      (XtPointer)XtOffsetOf(WidgetRec, core.colormap),
      sizeof(Colormap)
    },
    };

    XawInitializeWidgetSet();
    XtSetTypeConverter(XtRString, XtRColorCursor, XmuCvtStringToColorCursor,
		       convertArg, XtNumber(convertArg), XtCacheByDisplay, NULL);
}

static void
XawSimpleClassPartInitialize(WidgetClass cclass)
{
    SimpleWidgetClass c = (SimpleWidgetClass)cclass;
    SimpleWidgetClass super = (SimpleWidgetClass)c->core_class.superclass;

    if (c->simple_class.change_sensitive == NULL) {
	char buf[BUFSIZ];

	snprintf(buf, sizeof(buf),
		 "%s Widget: The Simple Widget class method "
		 "'change_sensitive' is undefined.\n"
		 "A function must be defined or inherited.",
		 c->core_class.class_name);
	XtWarning(buf);
	c->simple_class.change_sensitive = ChangeSensitive;
    }

    if (c->simple_class.change_sensitive == XtInheritChangeSensitive)
	c->simple_class.change_sensitive = super->simple_class.change_sensitive;
}

#ifndef OLDXAW
/*ARGSUSED*/
static void
XawSimpleInitialize(Widget request _X_UNUSED, Widget cnew,
		    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SimpleWidget simple = (SimpleWidget)cnew;

    if (simple->simple.tip)
	simple->simple.tip = XtNewString(simple->simple.tip);
}

static void
XawSimpleDestroy(Widget w)
{
    SimpleWidget simple = (SimpleWidget)w;

    if (simple->simple.tip)
	XtFree((XtPointer)simple->simple.tip);
}
#endif

static void
XawSimpleRealize(Widget w, Mask *valueMask, XSetWindowAttributes *attributes)
{
#ifndef OLDXAW
    XawPixmap *pixmap;
#endif
    Pixmap border_pixmap = CopyFromParent;

  if (!XtIsSensitive(w))
    {
	/* change border to gray; have to remember the old one,
	 * so XtDestroyWidget deletes the proper one */
	if (((SimpleWidget)w)->simple.insensitive_border == None)
	    ((SimpleWidget)w)->simple.insensitive_border =
		XmuCreateStippledPixmap(XtScreen(w),
					w->core.border_pixel,
					w->core.background_pixel,
					w->core.depth);
        border_pixmap = w->core.border_pixmap;
	attributes->border_pixmap =
	  w->core.border_pixmap = ((SimpleWidget)w)->simple.insensitive_border;

	*valueMask |= CWBorderPixmap;
	*valueMask &= (Mask)(~CWBorderPixel);
    }

    ConvertCursor(w);

    if ((attributes->cursor = ((SimpleWidget)w)->simple.cursor) != None)
	*valueMask |= CWCursor;

  XtCreateWindow(w, InputOutput, (Visual *)CopyFromParent,
		 *valueMask, attributes);

    if (!XtIsSensitive(w))
	w->core.border_pixmap = border_pixmap;

#ifndef OLDXAW
    if (w->core.background_pixmap > XtUnspecifiedPixmap) {
	pixmap = XawPixmapFromXPixmap(w->core.background_pixmap, XtScreen(w),
				      w->core.colormap, (int)w->core.depth);
	if (pixmap && pixmap->mask)
	    XawReshapeWidget(w, pixmap);
    }

    if (((SimpleWidget)w)->simple.tip)
	XawTipEnable(w);
#endif
}

/*
 * Function:
 *	ConvertCursor
 *
 * Parameters:
 *	w - simple widget
 *
 * Description:
 *	Converts a name to a new cursor.
 */
static void
ConvertCursor(Widget w)
{
    SimpleWidget simple = (SimpleWidget) w;
    XrmValue from, to;
    Cursor cursor = None;

    if (simple->simple.cursor_name == NULL)
	return;

    from.addr = (XPointer)simple->simple.cursor_name;
    from.size = (unsigned)strlen((char *)from.addr) + 1;

    to.size = sizeof(Cursor);
    to.addr = (XPointer)&cursor;

    if (XtConvertAndStore(w, XtRString, &from, XtRColorCursor, &to))
      simple->simple.cursor = cursor;
    else
	XtAppErrorMsg(XtWidgetToApplicationContext(w),
		      "convertFailed","ConvertCursor","XawError",
		      "Simple: ConvertCursor failed.",
		      NULL, NULL);
}


/*ARGSUSED*/
static Boolean
XawSimpleSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SimpleWidget s_old = (SimpleWidget)current;
    SimpleWidget s_new = (SimpleWidget)cnew;
    Bool new_cursor = False;

    /* this disables user changes after creation */
    s_new->simple.international = s_old->simple.international;

    if (XtIsSensitive(current) != XtIsSensitive(cnew))
	(*((SimpleWidgetClass)XtClass(cnew))->simple_class.change_sensitive)
	   (cnew);

    if (s_old->simple.cursor != s_new->simple.cursor)
	new_cursor = True;

    /*
     * We are not handling the string cursor_name correctly here
     */

    if (s_old->simple.pointer_fg != s_new->simple.pointer_fg ||
	s_old->simple.pointer_bg != s_new->simple.pointer_bg ||
	s_old->simple.cursor_name != s_new->simple.cursor_name) {
	ConvertCursor(cnew);
	new_cursor = True;
    }

    if (new_cursor && XtIsRealized(cnew)) {
	if (s_new->simple.cursor != None)
	    XDefineCursor(XtDisplay(cnew), XtWindow(cnew), s_new->simple.cursor);
	else
	    XUndefineCursor(XtDisplay(cnew), XtWindow(cnew));
      }

#ifndef OLDXAW
    if (s_old->core.background_pixmap != s_new->core.background_pixmap) {
	XawPixmap *opix, *npix;

	opix = XawPixmapFromXPixmap(s_old->core.background_pixmap,
				    XtScreen(s_old), s_old->core.colormap,
				    (int)s_old->core.depth);
	npix = XawPixmapFromXPixmap(s_new->core.background_pixmap,
				    XtScreen(s_new), s_new->core.colormap,
				    (int)s_new->core.depth);
	if ((npix && npix->mask) || (opix && opix->mask))
	    XawReshapeWidget(cnew, npix);
    }

    if (s_old->simple.tip != s_new->simple.tip) {
	if (s_old->simple.tip)
	    XtFree((XtPointer)s_old->simple.tip);
	if (s_new->simple.tip)
	    s_new->simple.tip = XtNewString(s_new->simple.tip);
    }

    if (s_old->simple.tip && !s_new->simple.tip)
	XawTipDisable(cnew);
    else if (!s_old->simple.tip && s_new->simple.tip)
	XawTipEnable(cnew);

    if (s_old->simple.display_list != s_new->simple.display_list)
	return (True);
#endif /* OLDXAW */

    return (False);
}

#ifndef OLDXAW
static void
XawSimpleExpose(Widget w, XEvent *event, Region region)
{
    SimpleWidget xaw = (SimpleWidget)w;

    if (xaw->simple.display_list)
	XawRunDisplayList(w, xaw->simple.display_list, event, region);
}
#endif

static Bool
ChangeSensitive(Widget w)
{
    if (XtIsRealized(w)) {
	if (XtIsSensitive(w))
	    if (w->core.border_pixmap != XtUnspecifiedPixmap)
	    XSetWindowBorderPixmap(XtDisplay(w), XtWindow(w),
				    w->core.border_pixmap);
	    else
		XSetWindowBorder(XtDisplay(w), XtWindow(w),
				 w->core.border_pixel);
	else {
	    if (((SimpleWidget)w)->simple.insensitive_border == None)
		((SimpleWidget)w)->simple.insensitive_border =
		    XmuCreateStippledPixmap(XtScreen(w),
					    w->core.border_pixel,
					    w->core.background_pixel,
					    w->core.depth);
	    XSetWindowBorderPixmap(XtDisplay(w), XtWindow(w),
				   ((SimpleWidget)w)->simple.insensitive_border);
	}
    }

    return (False);
}
