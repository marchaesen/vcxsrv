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

/*
 * Copyright (c) 1998 by The XFree86 Project, Inc.
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xatom.h>
#include <X11/Xfuncs.h>
#include <X11/Xutil.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xmu/Xmu.h>
#include <X11/Xaw/Cardinals.h>
#include <X11/Xaw/MultiSinkP.h>
#include <X11/Xaw/TextP.h>
#include <X11/Xaw/TextSrcP.h>
#include <X11/Xaw/TextSinkP.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/XawImP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"
#include "XawI18n.h"

#ifndef MAX_LEN_CT
#define MAX_LEN_CT	6	/* for sequence: ESC $ ( A \xx \xx */
#endif

unsigned long FMT8BIT = 0L;
unsigned long XawFmt8Bit = 0L;
unsigned long XawFmtWide = 0L;

#define SinkClearToBG		_XawTextSinkClearToBackground

#define SrcScan			XawTextSourceScan
#define SrcRead			XawTextSourceRead
#define SrcReplace		XawTextSourceReplace
#define SrcSearch		XawTextSourceSearch
#define SrcCvtSel		XawTextSourceConvertSelection
#define SrcSetSelection		XawTextSourceSetSelection

#define MULTI_CLICK_TIME	500L

#define SRC_CHANGE_NONE		0
#define SRC_CHANGE_AFTER	1
#define SRC_CHANGE_BEFORE	2
#define SRC_CHANGE_OVERLAP	3

#define Superclass (&simpleClassRec)

/*
 * Compute a the maximum length of a cut buffer that we can pass at any
 * time.  The 64 allows for the overhead of the Change Property request.
 */
#define MAX_CUT_LEN(dpy)  (XMaxRequestSize(dpy) - 64)

#define	ClearWindow(ctx)						     \
     _XawTextNeedsUpdating((ctx),					     \
			   (ctx)->text.lt.top,				     \
			   (ctx)->text.lt.info[ctx->text.lt.lines].position)

/*
 * Class Methods
 */
static void XawTextClassInitialize(void);
static void XawTextInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawTextRealize(Widget, XtValueMask*, XSetWindowAttributes*);
static void XawTextDestroy(Widget);
static void XawTextResize(Widget);
static void XawTextExpose(Widget, XEvent*, Region);
static Boolean XawTextSetValues(Widget, Widget, Widget, ArgList, Cardinal*);
static void XawTextGetValuesHook(Widget, ArgList, Cardinal*);
static Bool XawTextChangeSensitive(Widget);

/*
 * Prototypes
 */
static XawTextPosition _BuildLineTable(TextWidget, XawTextPosition, int);
static void _CreateCutBuffers(Display*);
static Boolean TextConvertSelection(Widget, Atom*, Atom*, Atom*, XtPointer*,
				unsigned long*, int*);
static int CountLines(TextWidget, XawTextPosition, XawTextPosition);
static void CreateHScrollBar(TextWidget);
static void CreateVScrollBar(TextWidget);
static void CvtStringToScrollMode(XrmValuePtr, Cardinal*,
				  XrmValuePtr, XrmValuePtr);
static Boolean CvtScrollModeToString(Display*, XrmValue*, Cardinal*,
				     XrmValue*, XrmValue*, XtPointer*);
static void CvtStringToWrapMode(XrmValuePtr, Cardinal*,
				XrmValuePtr, XrmValuePtr);
static Boolean CvtWrapModeToString(Display*, XrmValue*, Cardinal*,
				   XrmValue*, XrmValue*, XtPointer*);
static Boolean CvtStringToJustifyMode(Display*, XrmValue*, Cardinal*,
				      XrmValue*, XrmValue*, XtPointer*);
static Boolean CvtJustifyModeToString(Display*, XrmValue*, Cardinal*,
				      XrmValue*, XrmValue*, XtPointer*);
static void DestroyHScrollBar(TextWidget);
static void DestroyVScrollBar(TextWidget);
#ifndef OLDXAW
static void DisplayText(Widget, XawTextPosition, XawTextPosition);
#endif
static void OldDisplayText(Widget, XawTextPosition, XawTextPosition);
static void DisplayTextWindow(Widget);
static void DoCopyArea(TextWidget, int, int, unsigned int, unsigned int,
		       int, int);
static void DoSelection(TextWidget, XawTextPosition, Time, Bool);
static void ExtendSelection(TextWidget, XawTextPosition, Bool);
static XawTextPosition FindGoodPosition(TextWidget, XawTextPosition);
static void FlushUpdate(TextWidget);
static int GetCutBufferNumber(Atom);
static int GetMaxTextWidth(TextWidget);
static unsigned int GetWidestLine(TextWidget);
static void HScroll(Widget, XtPointer, XtPointer);
static void HJump(Widget, XtPointer, XtPointer);
static void InsertCursor(Widget, XawTextInsertState);
static Bool LineAndXYForPosition(TextWidget, XawTextPosition, int*,
				 int*, int*);
static int LineForPosition(TextWidget, XawTextPosition);
static void TextLoseSelection(Widget, Atom*);
static Bool MatchSelection(Atom, XawTextSelection*);
static void ModifySelection(TextWidget, XawTextPosition, XawTextPosition);
static XawTextPosition PositionForXY(TextWidget, int, int);
static void PositionHScrollBar(TextWidget);
static void PositionVScrollBar(TextWidget);
#ifndef OLDXAW
static int ResolveColumnNumber(TextWidget);
static int ResolveLineNumber(TextWidget);
#endif
static void _SetSelection(TextWidget, XawTextPosition, XawTextPosition,
			  Atom*, Cardinal);
static void TextSinkResize(Widget);
static void UpdateTextInRectangle(TextWidget, XRectangle*);
static void UpdateTextInLine(TextWidget, int, int, int);
static void VScroll(Widget, XtPointer, XtPointer);
static void VJump(Widget, XtPointer, XtPointer);

/*
 * External
 */
void _XawTextAlterSelection(TextWidget,
			    XawTextSelectionMode, XawTextSelectionAction,
			    String*, Cardinal*);
void _XawTextCheckResize(TextWidget);
void _XawTextClearAndCenterDisplay(TextWidget);
void _XawTextExecuteUpdate(TextWidget);
char *_XawTextGetText(TextWidget, XawTextPosition, XawTextPosition);
void _XawTextPrepareToUpdate(TextWidget);
int _XawTextReplace(TextWidget, XawTextPosition, XawTextPosition,
		    XawTextBlock*);
Atom *_XawTextSelectionList(TextWidget, String*, Cardinal);
void _XawTextSetScrollBars(TextWidget);
void _XawTextSetSelection(TextWidget, XawTextPosition, XawTextPosition,
			  String*, Cardinal);
void _XawTextVScroll(TextWidget, int);
void XawTextScroll(TextWidget, int, int);
void _XawTextSetSource(Widget, Widget, XawTextPosition, XawTextPosition);
#ifndef OLDXAW
void _XawTextSetLineAndColumnNumber(TextWidget, Bool);
#endif
void _XawTextSourceChanged(Widget, XawTextPosition, XawTextPosition,
			   XawTextBlock*, int);

/* Not used by other modules, but were extern on previous versions
 * of the library
 */
void _XawTextShowPosition(TextWidget);

/*
 * From TextAction.c
 */
extern void _XawTextZapSelection(TextWidget, XEvent*, Bool);

/*
 * From TextSrc.c
 */
void _XawSourceAddText(Widget, Widget);
void _XawSourceRemoveText(Widget, Widget, Bool);
Bool _XawTextSourceNewLineAtEOF(Widget);

/*
 * From TextSink.c
 */
void _XawTextSinkClearToBackground(Widget, int, int, unsigned, unsigned);
void _XawTextSinkDisplayText(Widget, int, int, XawTextPosition, XawTextPosition,
			     Bool);

/****************************************************************
 *
 * Full class record constant
 *
 ****************************************************************/
/*
 * From TextTr.c
 */
static XawTextSelectType defaultSelectTypes[] = {
  XawselectPosition,  XawselectAlphaNumeric, XawselectWord, XawselectLine,
  XawselectParagraph, XawselectAll,	     XawselectNull,
};

static XPointer defaultSelectTypesPtr = (XPointer)defaultSelectTypes;
static Dimension defWidth = 100;
static Dimension defHeight = DEFAULT_TEXT_HEIGHT;

#define offset(field) XtOffsetOf(TextRec, field)
static XtResource resources[] = {
  {
    XtNwidth,
    XtCWidth,
    XtRDimension,
    sizeof(Dimension),
    offset(core.width),
    XtRDimension,
    (XtPointer)&defWidth
  },
  {
    XtNcursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(simple.cursor),
    XtRString,
    "xterm"
  },
  {
    XtNheight,
    XtCHeight,
    XtRDimension,
    sizeof(Dimension),
    offset(core.height),
    XtRDimension,
    (XtPointer)&defHeight
  },
  {
    XtNdisplayPosition,
    XtCTextPosition,
    XtRInt,
    sizeof(XawTextPosition),
    offset(text.lt.top),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNinsertPosition,
    XtCTextPosition,
    XtRInt,
    sizeof(XawTextPosition),
    offset(text.insertPos),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNleftMargin,
    XtCMargin,
    XtRPosition,
    sizeof(Position),
    offset(text.r_margin.left),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNrightMargin,
    XtCMargin,
    XtRPosition,
    sizeof(Position),
    offset(text.r_margin.right),
    XtRImmediate,
    (XtPointer)4
  },
  {
    XtNtopMargin,
    XtCMargin,
    XtRPosition,
    sizeof(Position),
    offset(text.r_margin.top),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNbottomMargin,
    XtCMargin,
    XtRPosition,
    sizeof(Position),
    offset(text.r_margin.bottom),
    XtRImmediate,
    (XtPointer)2
  },
  {
    XtNselectTypes,
    XtCSelectTypes,
    XtRPointer,
    sizeof(XawTextSelectType*),
    offset(text.sarray),
    XtRPointer,
    (XtPointer)&defaultSelectTypesPtr
  },
  {
    XtNtextSource,
    XtCTextSource,
    XtRWidget,
    sizeof(Widget),
    offset(text.source),
    XtRImmediate,
    NULL
  },
  {
    XtNtextSink,
    XtCTextSink,
    XtRWidget,
    sizeof(Widget),
    offset(text.sink),
    XtRImmediate,
    NULL
  },
  {
    XtNdisplayCaret,
    XtCOutput,
    XtRBoolean,
    sizeof(Boolean),
    offset(text.display_caret),
    XtRImmediate,
    (XtPointer)True
  },
  {
    XtNscrollVertical,
    XtCScroll,
    XtRScrollMode,
    sizeof(XawTextScrollMode),
    offset(text.scroll_vert),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNscrollHorizontal,
    XtCScroll,
    XtRScrollMode,
    sizeof(XawTextScrollMode),
    offset(text.scroll_horiz),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNwrap,
    XtCWrap,
    XtRWrapMode,
    sizeof(XawTextWrapMode),
    offset(text.wrap),
    XtRImmediate,
    (XtPointer)XawtextWrapNever
  },
  {
    XtNautoFill,
    XtCAutoFill,
    XtRBoolean,
    sizeof(Boolean),
    offset(text.auto_fill),
    XtRImmediate,
    (XtPointer)False
  },
#ifndef OLDXAW
  {
    XtNpositionCallback,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(text.position_callbacks),
    XtRCallback,
    NULL
  },
  {
    XtNleftColumn,
    XtCColumn,
    XtRShort,
    sizeof(short),
    offset(text.left_column),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNrightColumn,
    XtCColumn,
    XtRShort,
    sizeof(short),
    offset(text.right_column),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNjustifyMode,
    XtCJustifyMode,
    XtRJustifyMode,
    sizeof(XawTextJustifyMode),
    offset(text.justify),
    XtRImmediate,
    (XtPointer)XawjustifyLeft
  },
#endif /* OLDXAW */
};
#undef offset

#define done(address, type) \
	{ toVal->size = sizeof(type); toVal->addr = (XPointer)address; }

static XrmQuark QWrapNever, QWrapLine, QWrapWord;
#ifndef notdef
static XrmQuark QScrollNever, QScrollWhenNeeded, QScrollAlways;
#endif
static XrmQuark QJustifyLeft, QJustifyRight, QJustifyCenter, QJustifyFull;

/*ARGSUSED*/
static void
CvtStringToScrollMode(XrmValuePtr args, Cardinal *num_args,
		      XrmValuePtr fromVal, XrmValuePtr toVal)
{
    static XawTextScrollMode scrollMode = XawtextScrollNever;
    XrmQuark q;
    char name[32];

    XmuNCopyISOLatin1Lowered(name, (char *)fromVal->addr, sizeof(name));
    q = XrmStringToQuark(name);

    if (q == QScrollNever || q == QScrollWhenNeeded)
	scrollMode = XawtextScrollNever;
    else if (q == QScrollAlways)
	scrollMode = XawtextScrollAlways;
    else if (strcmp(name, "true") == 0 || strcmp(name, "1") == 0)
	scrollMode = XawtextScrollAlways;
    else if (strcmp(name, "false") == 0 || strcmp(name, "0") == 0)
	scrollMode = XawtextScrollNever;
    else
	XtStringConversionWarning((char *)fromVal->addr, XtRScrollMode);

    done(&scrollMode, XawTextScrollMode);
}

/*ARGSUSED*/
static Boolean
CvtScrollModeToString(Display *dpy, XrmValue *args, Cardinal *num_args,
		      XrmValue *fromVal, XrmValue *toVal, XtPointer *data)
{
    static char *buffer;
    Cardinal size;

    switch (*(XawTextScrollMode *)fromVal->addr) {
	case XawtextScrollNever:
	case XawtextScrollWhenNeeded:
	    buffer = XtEtextScrollNever;
	    break;
	case XawtextScrollAlways:
	    buffer = XtEtextScrollAlways;
	    break;
	default:
	    XawTypeToStringWarning(dpy, XtRScrollMode);
	    toVal->addr = NULL;
	    toVal->size = 0;
	    return (False);
    }
    size = strlen(buffer) + 1;
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

/*ARGSUSED*/
static void
CvtStringToWrapMode(XrmValuePtr args, Cardinal *num_args,
		    XrmValuePtr fromVal, XrmValuePtr toVal)
{
    static XawTextWrapMode wrapMode = XawtextWrapNever;
    XrmQuark q;
    char lowerName[6];

    XmuNCopyISOLatin1Lowered(lowerName, (char *)fromVal->addr,
			     sizeof(lowerName));
    q = XrmStringToQuark(lowerName);

    if (q == QWrapNever)
	wrapMode = XawtextWrapNever;
    else if (q == QWrapLine)
	wrapMode = XawtextWrapLine;
    else if (q == QWrapWord)
	wrapMode = XawtextWrapWord;
    else
	XtStringConversionWarning((char *)fromVal->addr, XtRWrapMode);

    done(&wrapMode, XawTextWrapMode);
}

/*ARGSUSED*/
static Boolean
CvtWrapModeToString(Display *dpy, XrmValue *args, Cardinal *num_args,
		    XrmValue *fromVal, XrmValue *toVal, XtPointer *data)
{
    static char *buffer;
    Cardinal size;

    switch (*(XawTextWrapMode *)fromVal->addr) {
	case XawtextWrapNever:
	    buffer = XtEtextWrapNever;
	    break;
	case XawtextWrapLine:
	    buffer = XtEtextWrapLine;
	    break;
	case XawtextWrapWord:
	    buffer = XtEtextWrapWord;
	    break;
	default:
	    XawTypeToStringWarning(dpy, XtRWrapMode);
	    toVal->addr = NULL;
	    toVal->size = 0;
	    return (False);
    }
    size = strlen(buffer) + 1;
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

/*ARGSUSED*/
static Boolean
CvtStringToJustifyMode(Display *dpy, XrmValue *args, Cardinal *num_args,
		       XrmValue *fromVal, XrmValue *toVal, XtPointer *data)
{
    XawTextJustifyMode justify;
    XrmQuark q;
    char lowerName[8];

    XmuNCopyISOLatin1Lowered(lowerName, (char *)fromVal->addr,
			   sizeof(lowerName));
    q = XrmStringToQuark(lowerName);

    if (q == QJustifyLeft)
	justify = XawjustifyLeft;
    else if (q == QJustifyRight)
	justify = XawjustifyRight;
    else if (q == QJustifyCenter)
	justify = XawjustifyCenter;
    else if(q ==  QJustifyFull)
	justify = XawjustifyFull;
    else {
	XtStringConversionWarning((char *)fromVal->addr, XtRJustifyMode);
	return (False);
    }

    toVal->size = sizeof(XawTextJustifyMode);
    *(XawTextJustifyMode *)(toVal->addr) = justify;

    return (True);
}


/*ARGSUSED*/
static Boolean
CvtJustifyModeToString(Display *dpy, XrmValue *args, Cardinal *num_args,
		       XrmValue *fromVal, XrmValue *toVal, XtPointer *data)
{
    static char *buffer;
    Cardinal size;

    switch (*(XawTextJustifyMode *)fromVal->addr) {
	case XawjustifyLeft:
	    buffer = XtEtextJustifyLeft;
	    break;
	case XawjustifyRight:
	    buffer = XtEtextJustifyRight;
	    break;
	case XawjustifyCenter:
	    buffer = XtEtextJustifyCenter;
	    break;
	case XawjustifyFull:
	    buffer = XtEtextJustifyFull;
	    break;
	default:
	    XawTypeToStringWarning(dpy, XtRJustifyMode);
	    toVal->addr = NULL;
	    toVal->size = 0;
	    return (False);
    }
    size = strlen(buffer) + 1;
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

#undef done

static void
XawTextClassInitialize(void)
{
    if (!XawFmt8Bit)
	FMT8BIT = XawFmt8Bit = XrmPermStringToQuark("FMT8BIT");
    if (!XawFmtWide)
	XawFmtWide = XrmPermStringToQuark("FMTWIDE");

    XawInitializeWidgetSet();

    textClassRec.core_class.num_actions = _XawTextActionsTableCount;

    QWrapNever	= XrmPermStringToQuark(XtEtextWrapNever);
    QWrapLine	= XrmPermStringToQuark(XtEtextWrapLine);
    QWrapWord	= XrmPermStringToQuark(XtEtextWrapWord);
    XtAddConverter(XtRString, XtRWrapMode, CvtStringToWrapMode, NULL, 0);
    XtSetTypeConverter(XtRWrapMode, XtRString, CvtWrapModeToString,
		       NULL, 0, XtCacheNone, NULL);
    QScrollNever = XrmPermStringToQuark(XtEtextScrollNever);
    QScrollWhenNeeded = XrmPermStringToQuark(XtEtextScrollWhenNeeded);
    QScrollAlways = XrmPermStringToQuark(XtEtextScrollAlways);
    XtAddConverter(XtRString, XtRScrollMode, CvtStringToScrollMode,
		   NULL, 0);
    XtSetTypeConverter(XtRScrollMode, XtRString, CvtScrollModeToString,
		       NULL, 0, XtCacheNone, NULL);
    QJustifyLeft   = XrmPermStringToQuark(XtEtextJustifyLeft);
    QJustifyRight  = XrmPermStringToQuark(XtEtextJustifyRight);
    QJustifyCenter = XrmPermStringToQuark(XtEtextJustifyCenter);
    QJustifyFull   = XrmPermStringToQuark(XtEtextJustifyFull);
    XtSetTypeConverter(XtRString, XtRJustifyMode, CvtStringToJustifyMode,
		       NULL, 0, XtCacheNone, NULL);
    XtSetTypeConverter(XtRJustifyMode, XtRString, CvtJustifyModeToString,
		       NULL, 0, XtCacheNone, NULL);
}

/*
 * Function:
 *	PositionHScrollBar
 *
 * Parameters:
 *	ctx - text widget
 *
 * Description:
 *	Positions the Horizontal scrollbar.
 */
static void
PositionHScrollBar(TextWidget ctx)
{
    Widget hbar = ctx->text.hbar, vbar = ctx->text.vbar;
    Position x, y;
    Dimension width, height;

    if (ctx->text.hbar == NULL)
	return;

    if (vbar != NULL)
	x = XtWidth(vbar);
    else
	x = -XtBorderWidth(hbar);
    y = XtHeight(ctx) - XtHeight(hbar) - XtBorderWidth(hbar);
    if (vbar != NULL) {
	width = XtWidth(ctx) - XtWidth(vbar) - XtBorderWidth(vbar);
	if (width > XtWidth(ctx))
	    width = XtWidth(ctx);
    }
    else
	width = XtWidth(ctx);
    height = XtHeight(hbar);

    XtConfigureWidget(hbar, x, y, width, height, XtBorderWidth(hbar));
}

/*
 * Function:
 *	PositionVScrollBar
 *
 * Parameters:
 *	ctx - text widget
 *
 * Description:
 *	Positions the Vertical scrollbar.
 */
static void
PositionVScrollBar(TextWidget ctx)
{
    Widget vbar = ctx->text.vbar;
    Position x, y;
    Dimension width, height;

    if (vbar == NULL)
	return;

    x = y = -XtBorderWidth(vbar);
    height = XtHeight(ctx);
    width = XtWidth(vbar);

    XtConfigureWidget(vbar, x, y, width, height, XtBorderWidth(vbar));
}

static void
CreateVScrollBar(TextWidget ctx)
{
    Widget vbar;

    if (ctx->text.vbar != NULL)
	return;

    ctx->text.vbar = vbar =
	XtCreateWidget("vScrollbar", scrollbarWidgetClass, (Widget)ctx, NULL, 0);
    XtAddCallback(vbar, XtNscrollProc, VScroll, (XtPointer)ctx);
    XtAddCallback(vbar, XtNjumpProc, VJump, (XtPointer)ctx);

    ctx->text.r_margin.left += XtWidth(vbar) + XtBorderWidth(vbar);
    ctx->text.left_margin = ctx->text.margin.left = ctx->text.r_margin.left;

    PositionVScrollBar(ctx);
    PositionHScrollBar(ctx);
    TextSinkResize(ctx->text.sink);

    if (XtIsRealized((Widget)ctx)) {
	XtRealizeWidget(vbar);
	XtMapWidget(vbar);
    }
    XtSetKeyboardFocus(vbar, (Widget)ctx);
}

/*
 * Function:
 *	DestroyVScrollBar
 *
 * Parameters:
 *	ctx - parent text widget
 *
 * Description:
 *	Removes vertical ScrollBar.
 */
static void
DestroyVScrollBar(TextWidget ctx)
{
    Widget vbar = ctx->text.vbar;

    if (vbar == NULL)
	return;

    ctx->text.r_margin.left -= XtWidth(vbar) + XtBorderWidth(vbar);
    ctx->text.left_margin = ctx->text.margin.left = ctx->text.r_margin.left;

    XtDestroyWidget(vbar);
    ctx->text.vbar = NULL;
    if (!ctx->core.being_destroyed) {
	PositionHScrollBar(ctx);
	TextSinkResize(ctx->text.sink);
    }
}

static void
CreateHScrollBar(TextWidget ctx)
{
    Arg args[1];
    Widget hbar;
    int bottom;

    if (ctx->text.hbar != NULL)
	return;

    XtSetArg(args[0], XtNorientation, XtorientHorizontal);
    ctx->text.hbar = hbar =
	XtCreateWidget("hScrollbar", scrollbarWidgetClass, (Widget)ctx, args, 1);
    XtAddCallback(hbar, XtNscrollProc, HScroll, (XtPointer)ctx);
    XtAddCallback(hbar, XtNjumpProc, HJump, (XtPointer)ctx);

    bottom = ctx->text.r_margin.bottom + XtHeight(hbar) + XtBorderWidth(hbar);

    ctx->text.margin.bottom = ctx->text.r_margin.bottom = bottom;

    PositionHScrollBar(ctx);
    TextSinkResize(ctx->text.sink);

    if (XtIsRealized((Widget)ctx)) {
	XtRealizeWidget(hbar);
	XtMapWidget(hbar);
    }
    XtSetKeyboardFocus(hbar, (Widget)ctx);
}

/*
 * Function:
 *	DestroyHScrollBar
 *
 * Parameters:
 *	ctx - parent text widget
 *
 * Description:
 *	Removes horizontal ScrollBar.
 */
static void
DestroyHScrollBar(TextWidget ctx)
{
    Widget hbar = ctx->text.hbar;

    if (hbar == NULL)
	return;

    ctx->text.r_margin.bottom -= XtHeight(hbar) + XtBorderWidth(hbar);
    ctx->text.margin.bottom = ctx->text.r_margin.bottom;

    XtDestroyWidget(hbar);
    ctx->text.hbar = NULL;
    if (!ctx->core.being_destroyed)
	TextSinkResize(ctx->text.sink);
}

/*ARGSUSED*/
static void
XawTextInitialize(Widget request, Widget cnew,
		  ArgList args, Cardinal *num_args)
{
    TextWidget ctx = (TextWidget)cnew;

    ctx->text.lt.lines = 0;
    ctx->text.lt.info = (XawTextLineTableEntry *)
	XtCalloc(1, sizeof(XawTextLineTableEntry));
#ifndef OLDXAW
    ctx->text.lt.base_line = 1;
#endif
    (void)bzero(&ctx->text.origSel, sizeof(XawTextSelection));
    (void)bzero(&ctx->text.s, sizeof(XawTextSelection));
    ctx->text.s.type = XawselectPosition;
    ctx->text.salt = NULL;
    ctx->text.hbar = ctx->text.vbar = NULL;
    ctx->text.lasttime = 0;
    ctx->text.time = 0;
    ctx->text.showposition = True;
    ctx->text.lastPos = ctx->text.source != NULL ?
			XawTextGetLastPosition(ctx) : 0;
    ctx->text.file_insert = NULL;
    ctx->text.search = NULL;
    ctx->text.update = XmuNewScanline(0, 0, 0);
    ctx->text.gc = XtGetGC(cnew, 0, 0);
    ctx->text.hasfocus = False;
    ctx->text.margin = ctx->text.r_margin; /* Strucure copy */
    ctx->text.left_margin = ctx->text.r_margin.left;
    ctx->text.update_disabled = False;
    ctx->text.clear_to_eol = True;
    ctx->text.old_insert = -1;
    ctx->text.mult = 1;
    ctx->text.salt2 = NULL;
    ctx->text.from_left = -1;

#ifndef OLDXAW
    ctx->text.numeric = False;
    ctx->text.selection_state = False;
    ctx->text.kill_ring = 0;

    ctx->text.line_number = -1;
    ctx->text.column_number = -1;
    ctx->text.source_changed = SRC_CHANGE_NONE;

    ctx->text.kill_ring_ptr = NULL;
    ctx->text.overwrite = False;
#endif

    if (XtHeight(ctx) == DEFAULT_TEXT_HEIGHT) {
	XtHeight(ctx) = VMargins(ctx);
	if (ctx->text.sink != NULL)
	    XtHeight(ctx) += XawTextSinkMaxHeight(ctx->text.sink, 1);
    }

    if (ctx->text.scroll_vert == XawtextScrollAlways)
	CreateVScrollBar(ctx);
    if (ctx->text.scroll_horiz == XawtextScrollAlways)
	CreateHScrollBar(ctx);

#ifndef OLDXAW
    if (ctx->text.left_column < 0)
	ctx->text.left_column = 0;
    if (ctx->text.right_column < 0)
	ctx->text.right_column = 0;
#endif
}

static void
XawTextRealize(Widget w, XtValueMask *mask, XSetWindowAttributes *attr)
{
    TextWidget ctx = (TextWidget)w;

    (*textClassRec.core_class.superclass->core_class.realize)(w, mask, attr);

    if (ctx->text.hbar != NULL) {
	XtRealizeWidget(ctx->text.hbar);
	XtMapWidget(ctx->text.hbar);
    }

    if (ctx->text.vbar != NULL) {
	XtRealizeWidget(ctx->text.vbar);
	XtMapWidget(ctx->text.vbar);
    }

    _XawTextBuildLineTable(ctx, ctx->text.lt.top, True);

#ifndef OLDXAW
    _XawTextSetLineAndColumnNumber(ctx, True);
#endif
}

/* Utility routines for support of Text */
static void
_CreateCutBuffers(Display *d)
{
    static struct _DisplayRec {
	struct _DisplayRec *next;
	Display *dpy;
    } *dpy_list = NULL;
    struct _DisplayRec *dpy_ptr;

    for (dpy_ptr = dpy_list; dpy_ptr != NULL; dpy_ptr = dpy_ptr->next)
	if (dpy_ptr->dpy == d)
	    return;

    dpy_ptr = XtNew(struct _DisplayRec);
    dpy_ptr->next = dpy_list;
    dpy_ptr->dpy = d;
    dpy_list = dpy_ptr;

#define Create(buffer) \
  XChangeProperty(d, RootWindow(d, 0), buffer, XA_STRING, 8, \
		  PropModeAppend, NULL, 0);

    Create(XA_CUT_BUFFER0);
    Create(XA_CUT_BUFFER1);
    Create(XA_CUT_BUFFER2);
    Create(XA_CUT_BUFFER3);
    Create(XA_CUT_BUFFER4);
    Create(XA_CUT_BUFFER5);
    Create(XA_CUT_BUFFER6);
    Create(XA_CUT_BUFFER7);

#undef Create
}

/*
 * Procedure to manage insert cursor visibility for editable text.  It uses
 * the value of ctx->insertPos and an implicit argument. In the event that
 * position is immediately preceded by an eol graphic, then the insert cursor
 * is displayed at the beginning of the next line.
 */
static void
InsertCursor(Widget w, XawTextInsertState state)
{
    TextWidget ctx = (TextWidget)w;
    int x, y;
    int line;

    if (ctx->text.lt.lines < 1)
	return;

    if (ctx->text.display_caret &&
	LineAndXYForPosition(ctx, ctx->text.insertPos, &line, &x, &y)) {
	if (line < ctx->text.lt.lines)
	    y += (ctx->text.lt.info[line + 1].y - ctx->text.lt.info[line].y) + 1;
	else
	    y += (ctx->text.lt.info[line].y - ctx->text.lt.info[line - 1].y) + 1;

	XawTextSinkInsertCursor(ctx->text.sink, x, y, state);
    }

    /* Keep Input Method up to speed  */
    if (ctx->simple.international) {
	Arg list[1];

	XtSetArg(list[0], XtNinsertPosition, ctx->text.insertPos);
	_XawImSetValues(w, list, 1);
    }
}

/*
 * Procedure to register a span of text that is no longer valid on the display
 * It is used to avoid a number of small, and potentially overlapping, screen
 * updates.
*/
void
_XawTextNeedsUpdating(TextWidget ctx,
		      XawTextPosition left, XawTextPosition right)
{
    XmuSegment segment;

    if (left >= right)
	return;

    segment.x1 = (int)left;
    segment.x2 = (int)right;
    (void)XmuScanlineOrSegment(ctx->text.update, &segment);
}

/*
 * Procedure to read a span of text in Ascii form. This is purely a hack and
 * we probably need to add a function to sources to provide this functionality.
 * [note: this is really a private procedure but is used in multiple modules].
 */
char *
_XawTextGetText(TextWidget ctx, XawTextPosition left, XawTextPosition right)
{
    char *result, *tempResult;
    XawTextBlock text;
    int bytes;

    if (XawTextFormat(ctx, XawFmt8Bit))
	bytes = sizeof(unsigned char);
    else if (XawTextFormat(ctx, XawFmtWide))
	bytes = sizeof(wchar_t);
    else /* if there is another fomat, add here */
	bytes = 1;

    /* leave space for ZERO */
    tempResult = result = XtMalloc((unsigned)(right - left + ONE) * bytes);

    while (left < right) {
	left = SrcRead(ctx->text.source, left, &text, (int)(right - left));
	if (!text.length)
	    break;
	memmove(tempResult, text.ptr, (unsigned)(text.length * bytes));
	tempResult += text.length * bytes;
    }

    if (bytes == sizeof(wchar_t))
	*((wchar_t*)tempResult) = (wchar_t)0;
    else
	*tempResult = '\0';

    return (result);
}

/* Like _XawTextGetText, but enforces ICCCM STRING type encoding.  This
 * routine is currently used to put just the ASCII chars in the selection
 * into a cut buffer.
 */
char *
_XawTextGetSTRING(TextWidget ctx, XawTextPosition left, XawTextPosition right)
{
    unsigned char *s;
    unsigned char c;
    long i, j, n;
    wchar_t *ws, wc;

    /* allow ESC in accordance with ICCCM */
    if (XawTextFormat(ctx, XawFmtWide)) {
	MultiSinkObject sink = (MultiSinkObject)ctx->text.sink;
	ws = (wchar_t *)_XawTextGetText(ctx, left, right);
	n = wcslen(ws);
	for (j = 0, i = 0; j < n; j++) {
	    wc = ws[j];
	    if (XwcTextEscapement (sink->multi_sink.fontset, &wc, 1)
		|| (wc == _Xaw_atowc(XawTAB)) || (wc == _Xaw_atowc(XawLF))
		|| (wc == _Xaw_atowc(XawESC)))
		ws[i++] = wc;
	}
	ws[i] = (wchar_t)0;
	return ((char *)ws);
    }
    else {
	s = (unsigned char *)_XawTextGetText(ctx, left, right);
	/* only HT and NL control chars are allowed, strip out others */
	n = strlen((char *)s);
	i = 0;
	for (j = 0; j < n; j++)	{
	    c = s[j];
	    if (((c >= 0x20) && c <= 0x7f)
		||(c >= 0xa0) || (c == XawTAB) || (c == XawLF)
		|| (c == XawESC)) {
		s[i] = c;
		i++;
	    }
	}
	s[i] = 0;

	return ((char *)s);
    }
}

/*
 * This routine maps an x and y position in a window that is displaying text
 * into the corresponding position in the source.
 */
static XawTextPosition
PositionForXY(TextWidget ctx, int x, int y)
{
    int fromx, line, width, height;
    XawTextPosition position;

    if (ctx->text.lt.lines == 0)
	return (0);

    for (line = 0; line < ctx->text.lt.lines - 1; line++) {
	if (y <= ctx->text.lt.info[line + 1].y)
	    break;
    }
    position = ctx->text.lt.info[line].position;
    if (position >= ctx->text.lastPos)
	return (ctx->text.lastPos);
    fromx = ctx->text.left_margin;
    XawTextSinkFindPosition(ctx->text.sink, position, fromx, x - fromx,
			    False, &position, &width, &height);

    if (position > ctx->text.lastPos)
	return (ctx->text.lastPos);

    if (position >= ctx->text.lt.info[line + 1].position)
	position = SrcScan(ctx->text.source, ctx->text.lt.info[line + 1].position,
			   XawstPositions, XawsdLeft, 1, True);

    return (position);
}

/*
 * This routine maps a source position in to the corresponding line number
 * of the text that is displayed in the window.
 */
static int
LineForPosition(TextWidget ctx, XawTextPosition position)
{
    int line;

    for (line = 0; line < ctx->text.lt.lines; line++)
	if (position < ctx->text.lt.info[line + 1].position)
	    break;

    return (line);
}

/*
 * This routine maps a source position into the corresponding line number
 * and the x, y coordinates of the text that is displayed in the window.
 */
static Bool
LineAndXYForPosition(TextWidget ctx, XawTextPosition pos,
		     int *line, int *x, int *y)
{
    XawTextPosition linePos, endPos;
    Boolean visible;
    int realW, realH;

    *line = 0;
    *x = ctx->text.left_margin;
    *y = ctx->text.margin.top + 1;
    if ((visible = IsPositionVisible(ctx, pos)) != False) {
	*line = LineForPosition(ctx, pos);
	*y = ctx->text.lt.info[*line].y;
	linePos = ctx->text.lt.info[*line].position;
	XawTextSinkFindDistance(ctx->text.sink, linePos,
				*x, pos, &realW, &endPos, &realH);
	*x += realW;
    }

    return (visible);
}

/*
 * This routine builds a line table. It does this by starting at the
 * specified position and measuring text to determine the staring position
 * of each line to be displayed. It also determines and saves in the
 * linetable all the required metrics for displaying a given line (e.g.
 * x offset, y offset, line length, etc.).
 */
void
_XawTextBuildLineTable(TextWidget ctx, XawTextPosition position,
		       _XtBoolean force_rebuild)
{
    Dimension height = 0;
    int lines = 0;
    Cardinal size;

    if ((int)XtHeight(ctx) > VMargins(ctx)) {
	height = XtHeight(ctx) - VMargins(ctx);
	lines = XawTextSinkMaxLines(ctx->text.sink, height);
    }
    size = sizeof(XawTextLineTableEntry) * (lines + 1);

    if (lines != ctx->text.lt.lines || ctx->text.lt.info == NULL) {
	ctx->text.lt.info = (XawTextLineTableEntry *)
	    XtRealloc((char *)ctx->text.lt.info, size);
	ctx->text.lt.lines = lines;
	force_rebuild = True;
    }

    if (force_rebuild) {
	(void)bzero((char *)ctx->text.lt.info, size);
	/* force a text update in the first text line if it is visible */
	ctx->text.lt.info[0].position = (XawTextPosition)-1;
    }
    if (position != ctx->text.lt.info[0].position) {
	(void)_BuildLineTable(ctx, position, 0);
	ctx->text.clear_to_eol = True;
    }
}

/*
 * We may need to resize the line table here, since there maybe lines with
 * different fonts (that can be shorter or taller than the default one)
 */
static XawTextPosition
_BuildLineTable(TextWidget ctx, XawTextPosition position, int line)
{
    XawTextLineTableEntry *lt = ctx->text.lt.info + line;
    XawTextPosition end, update_from = -1;
    Position y;
    int wwidth, width, height;
#ifndef OLDXAW
    Widget src = ctx->text.source;
#endif
    int max_y = (int)XtHeight(ctx) - (int)ctx->text.margin.bottom;

    if (ctx->text.wrap == XawtextWrapNever)
	wwidth = 0x7fffffff;
    else
	wwidth = GetMaxTextWidth(ctx);

    /* XXX y may change, due to font size changes. See later */
    y = line == 0 ? ctx->text.margin.top : lt->y;

#ifndef OLDXAW
    if (ctx->text.lt.base_line < 0) {
	if (line == 0)
	    ctx->text.lt.top = position;
    }
    else if (line == 0) {
	XawTextPosition pos = ctx->text.lt.top;
	int base_line = ctx->text.lt.base_line;

	if (position == 0)
	    base_line = 1;
	else if (ctx->text.lt.base_line == 0 ||
		 ctx->text.source_changed == SRC_CHANGE_OVERLAP) {
	    pos = 0;
	    base_line = 1;

	    while (pos < position) {
		pos = SrcScan(src, pos, XawstEOL, XawsdRight, 1, True);
		if (pos <= position) {
		    ++base_line;
		    if (pos == ctx->text.lastPos) {
			base_line -= !_XawTextSourceNewLineAtEOF(src);
			break;
		    }
		}
	    }
	}
	else if (ctx->text.wrap == XawtextWrapNever
		 && IsPositionVisible(ctx, position))
	    base_line += LineForPosition(ctx, position);
	else if (pos < position) {
	    while (pos < position) {
		pos = SrcScan(src, pos, XawstEOL, XawsdRight, 1, True);
		if (pos <= position) {
		    ++base_line;
		    if (pos == ctx->text.lastPos) {
			base_line -= !_XawTextSourceNewLineAtEOF(src);
			break;
		    }
		}
	    }
	}
	else if (pos > position) {
	    while (pos > position) {
		pos = SrcScan(src, pos, XawstEOL, XawsdLeft, 1, False);
		if (--pos >= position)
		    --base_line;
	    }
	}

	ctx->text.lt.top = position;
	ctx->text.lt.base_line = base_line;
    }
#else
    if (line == 0)
	ctx->text.lt.top = position;
#endif

    /* CONSTCOND */
    while (True) {
	XawTextSinkFindPosition(ctx->text.sink, position, ctx->text.left_margin,
				wwidth, ctx->text.wrap == XawtextWrapWord,
				&end, &width, &height);

	if (lt->position != position) {
	    _XawTextNeedsUpdating(ctx, position,
				  end <= position ? position + 1 : end);
	    ctx->text.clear_to_eol = True;
	    lt->position = position;
	}
	if (lt->y != y) {
	    if (update_from < 0)
		update_from = line == 0 ?
		    ctx->text.lt.info[0].position :
		    ctx->text.lt.info[line - 1].position;
	    lt->y = y;
	    ctx->text.clear_to_eol = True;
	}
	if (lt->textWidth != width) {
	    if (lt->textWidth > width)
		ctx->text.clear_to_eol = True;
	    lt->textWidth = width;
	}
	y += height;

	if (end > ctx->text.lastPos) {
	    position = end;
	    ctx->text.clear_to_eol = True;
	    _XawTextNeedsUpdating(ctx, end, end + ctx->text.lt.lines - line);
	    while (line++ < ctx->text.lt.lines) {
		if (line > 1 && y > max_y) {
		    ctx->text.lt.lines = line - 1;
		    break;
		}
		++lt;
		if (lt->y != y) {
		    if (update_from < 0)
			update_from = line < 2 ?
			    ctx->text.lt.info[0].position :
			    ctx->text.lt.info[line - 2].position;
		    lt->y = y;
		}
		lt->position = ++position;
		lt->textWidth = 0;
		y += height;
	    }
	    if (update_from >= 0)
		_XawTextNeedsUpdating(ctx, update_from,
				      ctx->text.lt.info[ctx->text.lt.lines].position);
	    _XawTextSetScrollBars(ctx);

	    return (ctx->text.lastPos);
	}

	if (line && y > max_y)
	    /* will return in the next loop */
	    ctx->text.lt.lines = line;

	if (++line > ctx->text.lt.lines && y < max_y) {
	/* grow the line table */
	    ctx->text.lt.info = (XawTextLineTableEntry *)
		XtRealloc((char *)ctx->text.lt.info,
			  sizeof(XawTextLineTableEntry) * (line + 1));
	    lt = ctx->text.lt.info + line;
	    bzero(lt, sizeof(XawTextLineTableEntry));
	    ++ctx->text.lt.lines;
	}
	else
	    ++lt;
	if (position == end)
	    ++position;
	else
	    position = end;

	if (line > ctx->text.lt.lines) {
	    if (update_from >= 0)
		_XawTextNeedsUpdating(ctx, update_from,
				      ctx->text.lt.info[ctx->text.lt.lines].position);
	    _XawTextSetScrollBars(ctx);

	    return (position);
	}
    }
    /*NOTREACHED*/
}

/*
 * Function:
 *	GetWidestLine
 *
 * Parameters:
 *	ctx - text widget
 *
 * Description:
 *	  Returns the width (in pixels) of the widest line that
 *		     is currently visable.
 *
 * Returns:
 *	The width of the widest line
 */
static unsigned int
GetWidestLine(TextWidget ctx)
{
    int i;
    unsigned int widest;
    XawTextLineTablePtr lt = &(ctx->text.lt);

    for (i = 0, widest = 0; i < lt->lines; i++)
	if (widest < lt->info[i].textWidth)
	    widest = lt->info[i].textWidth;

    return (widest);
}

/*
 * This routine is used by Text to notify an associated scrollbar of the
 * correct metrics (position and shown fraction) for the text being currently
 * displayed in the window.
 */
void
_XawTextSetScrollBars(TextWidget ctx)
{
    float first, last, denom, widest;

    if (ctx->text.scroll_vert == XawtextScrollAlways) {
	if (ctx->text.lastPos == 0)
	    first = 0.0;
	else
	    first = ctx->text.lt.top / (float)ctx->text.lastPos;

	if (ctx->text.lt.info[ctx->text.lt.lines].position < ctx->text.lastPos)
	    last = ctx->text.lt.info[ctx->text.lt.lines].position /
		   (float)ctx->text.lastPos;
	else
	    last = 1.0;

	XawScrollbarSetThumb(ctx->text.vbar, first, last - first);
    }

    if (ctx->text.scroll_horiz == XawtextScrollAlways) {
	denom = GetWidestLine(ctx);
	if (denom <= 0)
	    denom = (int)XtWidth(ctx) - RHMargins(ctx);
	if (denom <= 0)
	    denom = 1;
	widest = ((int)XtWidth(ctx) - RHMargins(ctx)) / denom;
	first = ctx->text.r_margin.left - ctx->text.left_margin;
	first /= denom;

	XawScrollbarSetThumb(ctx->text.hbar, first, widest);
    }
}

static void
DoCopyArea(TextWidget ctx, int src_x, int src_y,
	   unsigned int width, unsigned int height, int dst_x, int dst_y)
{
    int x1, y1, x2, y2;

    x1 = ctx->text.r_margin.left;
    y1 = ctx->text.r_margin.top;
    x2 = XtWidth(ctx) - ctx->text.r_margin.right;
    y2 = XtHeight(ctx) - ctx->text.r_margin.bottom;

    if (x1 >= x2 || y1 >= y2)
	return;

    src_x = XawMax(x1, XawMin(src_x, x2));
    src_y = XawMax(y1, XawMin(src_y, y2));
    dst_x = XawMax(x1, XawMin(dst_x, x2));
    dst_y = XawMax(y1, XawMin(dst_y, y2));
    width = XawMax(0, XawMin(x2 - dst_x, (int)width));
    height = XawMax(0, XawMin(y2 - dst_y, (int)height));

    XCopyArea(XtDisplay(ctx), XtWindow(ctx), XtWindow(ctx), ctx->text.gc,
	      src_x, src_y, width, height, dst_x, dst_y);
}

/*
 * Function:
 *	XawTextScroll
 *
 * Parameters:
 *	ctx	- text widget
 *	vlines	- number of lines to scroll vertically
 *	hpixels	- number of pixels to scroll horizontally
 *
 * Description:
 *	Generic function for scrolling the text window.
 *	Allows vertical and horizontal scroll at the same time.
 */
void
XawTextScroll(TextWidget ctx, int vlines, int hpixels)
{
    XawTextPosition top, tmp, update_from, update_to;
    XawTextLineTable *lt;
    Arg arglist[1];
    int y0, y1, y2, count, dim, wwidth, lines = ctx->text.lt.lines;
    int vwidth, vheight;	/* visible width and height */
    Bool scroll;

    vwidth = (int)XtWidth(ctx) - RHMargins(ctx);
    vheight = (int)XtHeight(ctx) - RVMargins(ctx);
    lt = &ctx->text.lt;

    if (!lt || vwidth <= 0 || vheight <= 0)
	return;

    if ((scroll = ctx->core.background_pixmap == XtUnspecifiedPixmap) == True) {
	dim = lt->info[1].y - lt->info[0].y;
	for (count = 1; count < lt->lines - 1; count++)
	    if (lt->info[count + 1].y - lt->info[count].y != dim) {
		scroll = False;
		break;
	    }
    }

    wwidth = GetMaxTextWidth(ctx);

    /*
     * Do the horizontall scrolling
     */
    if (hpixels < 0 && ctx->text.left_margin - hpixels > ctx->text.r_margin.left)
	hpixels = ctx->text.left_margin - ctx->text.r_margin.left;
    ctx->text.left_margin -= hpixels;

    update_from = lt->top;	/* remember the old value */
    /*
     *	 Checks the requested number of lines and calculates the top
     * of the line table
     */
    if (vlines < 0) {		  /* VScroll Up */
	if (IsPositionVisible(ctx, 0))
	    vlines = 0;
	else if (ctx->text.wrap != XawtextWrapNever) {
	    XawTextPosition end;
	    int n_lines = 0;

	    count = -vlines;
	    end = lt->top;
	    while (n_lines < count) {
		top = SrcScan(ctx->text.source, end, XawstEOL,
			      XawsdLeft, 2, False);
		n_lines += CountLines(ctx, top, end);
		end = top;
	    }

	    while (count++ < n_lines) {
		tmp = top;
		XawTextSinkFindPosition(ctx->text.sink, top,
					ctx->text.left_margin,
					wwidth,ctx->text.wrap == XawtextWrapWord,
					&top, &dim, &dim);
		if (tmp == top)
		    ++top;
	    }
	}
	else
	    top = SrcScan(ctx->text.source, lt->top, XawstEOL,
			  XawsdLeft, -vlines + 1, False);
	if (-vlines >= ctx->text.lt.lines)
	  scroll = False;
     }
    else if (vlines > 0) {	  /* VScroll Down */
	if (LineForPosition(ctx, ctx->text.lastPos) == 0)
	    vlines = 0;
	if (vlines < lt->lines)
	    top = XawMin(lt->info[vlines].position, ctx->text.lastPos);
	else if (ctx->text.wrap == XawtextWrapNever)
	    top = SrcScan(ctx->text.source,
			  SrcScan(ctx->text.source, lt->top,
				  XawstEOL, XawsdRight, vlines,
				  True),
			  XawstEOL, XawsdLeft, 1, False);
	else {
	    top = lt->top;
	    count = 0;
	    while (count++ < vlines) {
		tmp = top;
		XawTextSinkFindPosition(ctx->text.sink, top,
					ctx->text.left_margin,
					wwidth, ctx->text.wrap == XawtextWrapWord,
					&top, &dim, &dim);
		if (tmp == top)
		    ++top;
	    }
	}
	if (vlines >= ctx->text.lt.lines
	    || lt->info[vlines].position >= ctx->text.lastPos)
	    scroll = False;
    }

    if (!vlines) {
	if (hpixels) {
	    ClearWindow(ctx);
	    ctx->text.clear_to_eol = True;
	}
	_XawTextSetScrollBars(ctx);
	return;
    }

    /* Flushes any pending updates. Normally, there may be a call to
     * XawTextUnsetSelection not yet updated.
     */
    if (!hpixels && scroll) {
	ctx->text.clear_to_eol = True;
	FlushUpdate(ctx);
    }

    /*
     * Rebuild the line table, doing the vertical scroll
     */
    (void)_BuildLineTable(ctx, top, 0);
    lt = &ctx->text.lt;
    if (scroll) {
	for (count = 0; count < lt->lines - 1; count++)
	    if (lt->info[count + 1].y - lt->info[count].y != dim) {
		scroll = False;
		break;
	    }
    }

    XtSetArg(arglist[0], XtNinsertPosition, lt->top + lt->lines);
    _XawImSetValues((Widget)ctx, arglist, 1);

    if (hpixels || !scroll || lines != lt->lines)
	return;

    /* _BuildLineTable updates everything if the top position changes.
     * It is not required here.
     */
    (void)XmuScanlineXor(ctx->text.update, ctx->text.update);
    if (vlines < 0 && IsPositionVisible(ctx, 0))
	vlines = -LineForPosition(ctx, update_from);

    y0 = ctx->text.r_margin.top;
    if (vlines < 0) {
	update_from = lt->top;
	update_to = lt->info[-vlines + 1].position - 1;
	y1 = lt->info[lt->lines + vlines].y;
	y2 = lt->info[-vlines].y;
	DoCopyArea(ctx, ctx->text.r_margin.left, y0, vwidth,
		   y1 - y0,
		   ctx->text.r_margin.left, y2);
    }
    else {
	update_from = lt->info[lt->lines - vlines].position;
	update_to = lt->info[lt->lines].position;
	y1 = lt->info[lt->lines - vlines].y;
	y2 = lt->info[vlines].y;
	DoCopyArea(ctx, ctx->text.r_margin.left, y2,
		   vwidth, lt->info[lt->lines].y - y2,
		   ctx->text.r_margin.left, y0);
    }
    _XawTextNeedsUpdating(ctx, update_from, update_to);
    ctx->text.clear_to_eol = True;
}

/*
 * The routine will scroll the displayed text by lines.  If the arg  is
 * positive, move up; otherwise, move down. [note: this is really a private
 * procedure but is used in multiple modules].
 */
void
_XawTextVScroll(TextWidget ctx, int n)
{
  XawTextScroll(ctx, n, 0);
}

/*ARGSUSED*/
static void
HScroll(Widget w, XtPointer closure, XtPointer callData)
{
    TextWidget ctx = (TextWidget)closure;
    long pixels = (long)callData;

    if (pixels > 0) {
	long max;

	max = (int)GetWidestLine(ctx) + ctx->text.left_margin -
	      ctx->text.r_margin.left;
	max = XawMax(0, max);
	pixels = XawMin(pixels, max);
    }

    if (pixels) {
	_XawTextPrepareToUpdate(ctx);
	XawTextScroll(ctx, 0, pixels);
	_XawTextExecuteUpdate(ctx);
    }
}

/*ARGSUSED*/
static void
HJump(Widget w, XtPointer closure, XtPointer callData)
{
    TextWidget ctx = (TextWidget)closure;
    float percent = *(float *)callData;
    long pixels;

    pixels = ctx->text.left_margin -
	     (ctx->text.r_margin.left - (int)(percent * GetWidestLine(ctx)));

    HScroll(w, (XtPointer)ctx, (XtPointer)pixels);
}

/*
 * Function:
 *	UpdateTextInLine
 *
 * Parameters:
 *	ctx  - text widget
 *	line - line to update
 *	x1   - left pixel
 *	x2   - right pixel
 *
 * Description:
 *	Updates the text in the given line and pixel interval
 */
static void
UpdateTextInLine(TextWidget ctx, int line, int x1, int x2)
{
    XawTextLineTableEntry *lt = ctx->text.lt.info + line;
    XawTextPosition left, right;
    int from_x, width, height;

    if (lt->position >= ctx->text.lastPos
	|| ctx->text.left_margin > x2
	|| (int)lt->textWidth + ctx->text.left_margin < x1) {
	/* Mark line to be cleared */
	if (ctx->text.clear_to_eol)
	    _XawTextNeedsUpdating(ctx, lt->position, lt->position + 1);
	return;
    }

    from_x = ctx->text.left_margin;
    XawTextSinkFindPosition(ctx->text.sink, lt->position,
			    from_x, x1 - from_x,
			    False, &left, &width, &height);
    if (line == ctx->text.lt.lines)
	right = -1;
    else if (x2 >= lt->textWidth - from_x)
	right = lt[1].position - 1;
    else {
	from_x += width;
	XawTextSinkFindPosition(ctx->text.sink, left,
				from_x, x2 - from_x,
				False, &right, &width, &height);
    }

    if ((right < 0) || (right + 1 <= lt[1].position))
	++right;

    /* Mark text interval to be repainted */
    _XawTextNeedsUpdating(ctx, left, right);
}

/*
 * The routine will scroll the displayed text by pixels.  If the calldata is
 * positive, move up; otherwise, move down.
 */
/*ARGSUSED*/
static void
VScroll(Widget w, XtPointer closure, XtPointer callData)
{
    TextWidget ctx = (TextWidget)closure;
    long height, lines = (long)callData;

    height = XtHeight(ctx) - VMargins(ctx);
    if (height < 1)
	height = 1;
    lines = (lines * ctx->text.lt.lines) / height;
    _XawTextPrepareToUpdate(ctx);
    XawTextScroll(ctx, lines, 0);
    _XawTextExecuteUpdate(ctx);
}

/*ARGSUSED*/
static void
VJump(Widget w, XtPointer closure, XtPointer callData)
{
    float percent = *(float *)callData;
    TextWidget ctx = (TextWidget)closure;
    XawTextPosition top, last, position, tmp;
    XawTextLineTable *lt = &(ctx->text.lt);
    int dim, vlines = 0, wwidth = GetMaxTextWidth(ctx);
    Bool scroll = True;

    position = percent * ctx->text.lastPos;
    top = lt->top;

    if (!lt->lines || (position >= lt->top && position < lt->info[1].position)) {
	_XawTextSetScrollBars(ctx);
	return;
    }

#ifndef OLDXAW
    ctx->text.lt.base_line = -1;
#endif

    if (position > lt->top) {	  /* VScroll Up */
	if (position > lt->top && position < lt->info[lt->lines].position)
	    vlines = LineForPosition(ctx, position);
	else {
	    scroll = False;
	    top = SrcScan(ctx->text.source, position, XawstEOL,
			  XawsdLeft, 1, False);
	    if (ctx->text.wrap != XawtextWrapNever) {
		last = top;
		while (last < position) {
		    tmp = last;
		    XawTextSinkFindPosition(ctx->text.sink, last,
					    ctx->text.left_margin, wwidth,
					    ctx->text.wrap == XawtextWrapWord,
					    &last, &dim, &dim);
		    if (last == tmp)
			++last;
		    if (last < position)
			top = last;
		}
	    }
	}
    }
    else {		  /* VScroll Down */
	/*
	 * Calculates the number of lines
	 */
	while (top > position) {
	    last = top;
	    top = SrcScan(ctx->text.source, top, XawstEOL,
			  XawsdLeft, 2, False);
	    vlines -= CountLines(ctx, top, last);
	    if (-vlines >= ctx->text.lt.lines) {
		scroll = False;
		top = SrcScan(ctx->text.source, position, XawstEOL,
			      XawsdLeft, 1, False);
		break;
	      }
	  }
	/*
	 * Normalize
	 */
	if (ctx->text.wrap != XawtextWrapNever) {
	    last = top;
	    while (last < position) {
		tmp = last;
		XawTextSinkFindPosition(ctx->text.sink, last,
					ctx->text.left_margin,
					wwidth,
					ctx->text.wrap == XawtextWrapWord,
					&last, &dim, &dim);
		if (last == tmp)
		    ++last;
		if (last < position)
		    top = last;
		++vlines;
	    }
	}
    }

    if (vlines || !scroll) {
	_XawTextPrepareToUpdate(ctx);
	if (scroll)
	    XawTextScroll(ctx, vlines, 0);
	else
	    _BuildLineTable(ctx, top, 0);
	_XawTextExecuteUpdate(ctx);
    }
}

static Bool
MatchSelection(Atom selection, XawTextSelection *s)
{
    Atom *match;
    int count;

    for (count = 0, match = s->selections; count < s->atom_count;
       match++, count++)
    if (*match == selection)
	return (True);

  return (False);
}

static Boolean
TextConvertSelection(Widget w, Atom *selection, Atom *target, Atom *type,
		 XtPointer *value, unsigned long *length, int *format)
{
    Display *d = XtDisplay(w);
    TextWidget ctx = (TextWidget)w;
    Widget src = ctx->text.source;
    XawTextEditType edit_mode;
    Arg args[1];
    XawTextSelectionSalt *salt = NULL;
    XawTextSelection *s;

    if (*target == XA_TARGETS(d)) {
	Atom *targetP, *std_targets;
	unsigned long std_length;

	if (SrcCvtSel(src, selection, target, type, value, length, format))
	    return (True);

	XtSetArg(args[0], XtNeditType, &edit_mode);
	XtGetValues(src, args, ONE);

	XmuConvertStandardSelection(w, ctx->text.time, selection,
				    target, type, (XPointer*)&std_targets,
				    &std_length, format);

	*length = 7 + (edit_mode == XawtextEdit) + std_length;
	*value = XtMalloc((unsigned)sizeof(Atom)*(*length));
	targetP = *(Atom**)value;
	*targetP++ = XA_STRING;
	*targetP++ = XA_TEXT(d);
	*targetP++ = XA_UTF8_STRING(d);
	*targetP++ = XA_COMPOUND_TEXT(d);
	*targetP++ = XA_LENGTH(d);
	*targetP++ = XA_LIST_LENGTH(d);
	*targetP++ = XA_CHARACTER_POSITION(d);
	if (edit_mode == XawtextEdit) {
	    *targetP++ = XA_DELETE(d);
	}
	(void)memmove((char*)targetP, (char*)std_targets,
		      sizeof(Atom) * std_length);
	XtFree((char*)std_targets);
	*type = XA_ATOM;
	*format = 32;
	return (True);
    }

    if (SrcCvtSel(src, selection, target, type, value, length, format))
	return (True);

    if (MatchSelection(*selection, &ctx->text.s))
	s = &ctx->text.s;
    else {
	for (salt = ctx->text.salt; salt; salt = salt->next)
	    if (MatchSelection(*selection, &salt->s))
		break;
	if (!salt)
	    return (False);
	s = &salt->s;
    }
    if (*target == XA_STRING
	|| *target == XA_TEXT(d)
	|| *target == XA_UTF8_STRING(d)
	|| *target == XA_COMPOUND_TEXT(d)) {
	if (*target == XA_TEXT(d)) {
	    if (XawTextFormat(ctx, XawFmtWide))
		*type = XA_COMPOUND_TEXT(d);
	    else
		*type = XA_STRING;
	}
	else
	    *type = *target;
	/*
	 * If salt is True, the salt->contents stores CT string,
	 * its length is measured in bytes.
	 * Refer to _XawTextSaltAwaySelection().
	 *
	 * by Li Yuhong, Mar. 20, 1991.
	 */
	if (!salt) {
	    *value = _XawTextGetSTRING(ctx, s->left, s->right);
	    if (XawTextFormat(ctx, XawFmtWide)) {
		XTextProperty textprop;
		if (XwcTextListToTextProperty(d, (wchar_t **)value, 1,
					      XCompoundTextStyle, &textprop)
		    <  Success) {
		    XtFree((char *)*value);
		    return (False);
		}
		XtFree((char *)*value);
		*value = (XtPointer)textprop.value;
		*length = textprop.nitems;
	    }
	    else
	        *length = strlen((char *)*value);
	}
	else {
	    *value = XtMalloc((salt->length + 1) * sizeof(unsigned char));
	    strcpy ((char *)*value, salt->contents);
	    *length = salt->length;
	}
	/* Got *value,*length, now in COMPOUND_TEXT format. */
	if (XawTextFormat(ctx, XawFmtWide) && *type == XA_STRING) {
	    XTextProperty textprop;
	    wchar_t **wlist;
	    int count;

	    textprop.encoding = XA_COMPOUND_TEXT(d);
	    textprop.value = (unsigned char *)*value;
	    textprop.nitems = strlen(*value);
	    textprop.format = 8;
	    if (XwcTextPropertyToTextList(d, &textprop, &wlist, &count)
		 < Success
		|| count < 1) {
		XtFree((char *)*value);
		return (False);
	    }
	    XtFree((char *)*value);
	    if (XwcTextListToTextProperty(d, wlist, 1, XStringStyle, &textprop)
		 < Success) {
		XwcFreeStringList((wchar_t**) wlist);
		return (False);
	    }
	    *value = (XtPointer)textprop.value;
	    *length = textprop.nitems;
	    XwcFreeStringList(wlist);
	} else if (*type == XA_UTF8_STRING(d)) {
	    XTextProperty textprop;
	    char **list;
	    int count;

	    textprop.encoding = XA_COMPOUND_TEXT(d);
	    textprop.value = (unsigned char *)*value;
	    textprop.nitems = strlen(*value);
	    textprop.format = 8;
	    if (Xutf8TextPropertyToTextList(d, &textprop, &list, &count)
		 < Success
		|| count < 1) {
		XtFree((char *)*value);
		return (False);
	    }
	    XtFree((char *)*value);
	    *value = *list;
	    *length = strlen(*list);
	    XFree(list);
	}
	*format = 8;
	return (True);
    }

    if ((*target == XA_LIST_LENGTH(d)) || (*target == XA_LENGTH(d))) {
	long * temp;

	temp = (long *)XtMalloc((unsigned)sizeof(long));
	if (*target == XA_LIST_LENGTH(d))
	    *temp = 1L;
	else			/* *target == XA_LENGTH(d) */
	    *temp = (long) (s->right - s->left);

	*value = (XPointer)temp;
	*type = XA_INTEGER;
	*length = 1L;
	*format = 32;
	return (True);
    }

    if (*target == XA_CHARACTER_POSITION(d)) {
	long * temp;

	temp = (long *)XtMalloc((unsigned)(2 * sizeof(long)));
	temp[0] = (long)(s->left + 1);
	temp[1] = s->right;
	*value = (XPointer)temp;
	*type = XA_SPAN(d);
	*length = 2L;
	*format = 32;
	return (True);
    }

    if (*target == XA_DELETE(d)) {
	if (!salt)
	    _XawTextZapSelection(ctx, NULL, True);
	*value = NULL;
	*type = XA_NULL(d);
	*length = 0;
	*format = 32;
	return (True);
    }

    if (XmuConvertStandardSelection(w, ctx->text.time, selection, target, type,
				    (XPointer *)value, length, format))
	return (True);

    /* else */
    return (False);
}

/*
 * Function:
 *	GetCutBuffferNumber
 *
 * Parameters:
 *	atom - atom to check
 *
 * Description:
 *	Returns the number of the cut buffer.
 *
 * Returns:
 *	The number of the cut buffer representing this atom or NOT_A_CUT_BUFFER
 */
#define NOT_A_CUT_BUFFER -1
static int
GetCutBufferNumber(Atom atom)
{
    if (atom == XA_CUT_BUFFER0) return (0);
    if (atom == XA_CUT_BUFFER1) return (1);
    if (atom == XA_CUT_BUFFER2) return (2);
    if (atom == XA_CUT_BUFFER3) return (3);
    if (atom == XA_CUT_BUFFER4) return (4);
    if (atom == XA_CUT_BUFFER5) return (5);
    if (atom == XA_CUT_BUFFER6) return (6);
    if (atom == XA_CUT_BUFFER7) return (7);
    return (NOT_A_CUT_BUFFER);
}

static void
TextLoseSelection(Widget w, Atom *selection)
{
    TextWidget ctx = (TextWidget)w;
    Atom *atomP;
    int i;
    XawTextSelectionSalt*salt, *prevSalt, *nextSalt;

    atomP = ctx->text.s.selections;
    for (i = 0 ; i < ctx->text.s.atom_count; i++, atomP++)
	if ((*selection == *atomP)
	    || (GetCutBufferNumber(*atomP) != NOT_A_CUT_BUFFER))
	    *atomP = (Atom)0;

    while (ctx->text.s.atom_count
	   && ctx->text.s.selections[ctx->text.s.atom_count - 1] == 0)
	ctx->text.s.atom_count--;

    /*
     * Must walk the selection list in opposite order from UnsetSelection
     */
    atomP = ctx->text.s.selections;
    for (i = 0 ; i < ctx->text.s.atom_count; i++, atomP++)
	if (*atomP == (Atom)0) {
	    *atomP = ctx->text.s.selections[--ctx->text.s.atom_count];
	    while (ctx->text.s.atom_count
		   && ctx->text.s.selections[ctx->text.s.atom_count-1] == 0)
		ctx->text.s.atom_count--;
	}

    if (ctx->text.s.atom_count == 0)
	ModifySelection(ctx, ctx->text.insertPos, ctx->text.insertPos);

    prevSalt = 0;
    for (salt = ctx->text.salt; salt; salt = nextSalt) {
	atomP = salt->s.selections;
	nextSalt = salt->next;
	for (i = 0 ; i < salt->s.atom_count; i++, atomP++)
	    if (*selection == *atomP)
		*atomP = (Atom)0;

	while (salt->s.atom_count
	       && salt->s.selections[salt->s.atom_count-1] == 0)
	    salt->s.atom_count--;

	/*
	 * Must walk the selection list in opposite order from UnsetSelection
	 */
	atomP = salt->s.selections;
	for (i = 0 ; i < salt->s.atom_count; i++, atomP++)
	    if (*atomP == (Atom)0) {
		*atomP = salt->s.selections[--salt->s.atom_count];
		while (salt->s.atom_count
		       && salt->s.selections[salt->s.atom_count-1] == 0)
		    salt->s.atom_count--;
	    }

	if (salt->s.atom_count == 0) {
	    XtFree ((char *) salt->s.selections);
	    XtFree (salt->contents);
	    if (prevSalt)
		prevSalt->next = nextSalt;
	    else
		ctx->text.salt = nextSalt;
	    XtFree((char *)salt);
	}
	else
	    prevSalt = salt;
    }
}

void
_XawTextSaltAwaySelection(TextWidget ctx, Atom *selections, int num_atoms)
{
    XawTextSelectionSalt *salt;
    int i, j;

    for (i = 0; i < num_atoms; i++)
	TextLoseSelection((Widget)ctx, selections + i);
    if (num_atoms == 0)
	return;
    salt = (XawTextSelectionSalt *)
	XtMalloc((unsigned)sizeof(XawTextSelectionSalt));
    if (!salt)
	return;
    salt->s.selections = (Atom *)XtMalloc((unsigned)(num_atoms * sizeof(Atom)));
    if (!salt->s.selections) {
	XtFree((char *)salt);
	return;
    }
    salt->s.left = ctx->text.s.left;
    salt->s.right = ctx->text.s.right;
    salt->s.type = ctx->text.s.type;
    salt->contents = _XawTextGetSTRING(ctx, ctx->text.s.left, ctx->text.s.right);
    if (XawTextFormat(ctx, XawFmtWide)) {
	XTextProperty textprop;
	if (XwcTextListToTextProperty(XtDisplay((Widget)ctx),
				      (wchar_t**)(&(salt->contents)), 1,
				      XCompoundTextStyle,
				      &textprop) < Success) {
	    XtFree(salt->contents);
	    salt->length = 0;
	    return;
	}
	XtFree(salt->contents);
	salt->contents = (char *)textprop.value;
	salt->length = textprop.nitems;
    }
    else
	salt->length = strlen (salt->contents);
    salt->next = ctx->text.salt;
    ctx->text.salt = salt;
    j = 0;
    for (i = 0; i < num_atoms; i++) {
	if (GetCutBufferNumber (selections[i]) == NOT_A_CUT_BUFFER) {
	    salt->s.selections[j++] = selections[i];
	    XtOwnSelection((Widget)ctx, selections[i], ctx->text.time,
			   TextConvertSelection, TextLoseSelection, NULL);
	}
    }
    salt->s.atom_count = j;
}

static void
_SetSelection(TextWidget ctx, XawTextPosition left, XawTextPosition right,
	      Atom *selections, Cardinal count)
{
#ifndef OLDXAW
    Cardinal i;
    XawTextPosition pos;
    TextSrcObject src = (TextSrcObject)ctx->text.source;

    for (i = 0; i < src->textSrc.num_text; i++) {
	TextWidget tw = (TextWidget)src->textSrc.text[i];
	Bool needs_updating = tw->text.old_insert < 0;
	Bool showposition = tw->text.showposition;

	if (needs_updating) {
	    tw->text.showposition = False;
	    _XawTextPrepareToUpdate(tw);
	}
#else
	TextWidget tw = ctx;
	XawTextPosition pos;
#endif /* OLDXAW */

	if (left < tw->text.s.left) {
	    pos = Min(right, tw->text.s.left);
	    _XawTextNeedsUpdating(tw, left, pos);
	}
	if (left > tw->text.s.left) {
	    pos = Min(left, tw->text.s.right);
	    _XawTextNeedsUpdating(tw, tw->text.s.left, pos);
	}
	if (right < tw->text.s.right) {
	    pos = Max(right, tw->text.s.left);
	    _XawTextNeedsUpdating(tw, pos, tw->text.s.right);
	}
	if (right > tw->text.s.right) {
	    pos = Max(left, tw->text.s.right);
	    _XawTextNeedsUpdating(tw, pos, right);
	}

	tw->text.s.left = left;
	tw->text.s.right = right;

#ifndef OLDXAW
	if (needs_updating) {
	    _XawTextExecuteUpdate(tw);
	    tw->text.showposition = showposition;
	}
    }
#endif /* OLDXAW */

    SrcSetSelection(ctx->text.source, left, right,
		    (count == 0) ? None : selections[0]);

    if (left < right) {
	Widget w = (Widget)ctx;
	int buffer;

	while (count) {
	    Atom selection = selections[--count];

	    /*
	     * If this is a cut buffer
	     */
	    if ((buffer = GetCutBufferNumber(selection)) != NOT_A_CUT_BUFFER) {
		unsigned char *ptr, *tptr;
		unsigned int amount, max_len = MAX_CUT_LEN(XtDisplay(w));
		unsigned long len;

		tptr= ptr= (unsigned char *)_XawTextGetSTRING(ctx,
							      ctx->text.s.left,
							      ctx->text.s.right);
		if (XawTextFormat(ctx, XawFmtWide)) {
		    /*
		     * Only XA_STRING(Latin 1) is allowed in CUT_BUFFER,
		     * so we get it from wchar string, then free the wchar string
		     */
		    XTextProperty textprop;

		    if (XwcTextListToTextProperty(XtDisplay(w), (wchar_t**)&ptr,
						  1, XStringStyle, &textprop)
			<  Success){
			XtFree((char *)ptr);
			return;
		    }
		    XtFree((char *)ptr);
		    tptr = ptr = textprop.value;
		}
		if (buffer == 0) {
		    _CreateCutBuffers(XtDisplay(w));
		    XRotateBuffers(XtDisplay(w), 1);
		}
		amount = Min ((len = strlen((char *)ptr)), max_len);
		XChangeProperty(XtDisplay(w), RootWindow(XtDisplay(w), 0),
				selection, XA_STRING, 8, PropModeReplace,
				ptr, amount);

		while (len > max_len) {
		    len -= max_len;
		    tptr += max_len;
		    amount = Min (len, max_len);
		    XChangeProperty(XtDisplay(w), RootWindow(XtDisplay(w), 0),
				    selection, XA_STRING, 8, PropModeAppend,
				    tptr, amount);
		}
		XtFree ((char *)ptr);
	    }
	    else		/* This is a real selection */
		XtOwnSelection(w, selection, ctx->text.time, TextConvertSelection,
			       TextLoseSelection, NULL);
	}
    }
    else
	XawTextUnsetSelection((Widget)ctx);
}

#ifndef OLDXAW
void
_XawTextSetLineAndColumnNumber(TextWidget ctx, Bool force)
{
    int line_number, column_number;

    if (ctx->text.old_insert != ctx->text.insertPos &&
	ctx->text.lt.base_line < 0) {
	ctx->text.lt.base_line = 0;
	(void)_BuildLineTable(ctx, ctx->text.lt.top, 0);
    }

    line_number = ResolveLineNumber(ctx);
    column_number = ResolveColumnNumber(ctx);

    if (force || (ctx->text.column_number != column_number
		  || ctx->text.line_number != line_number)) {
	XawTextPositionInfo info;

	ctx->text.line_number = info.line_number = line_number;
	ctx->text.column_number = info.column_number = column_number;
	info.insert_position = ctx->text.insertPos;
	info.last_position = ctx->text.lastPos;
	info.overwrite_mode = ctx->text.overwrite;

	XtCallCallbacks((Widget)ctx, XtNpositionCallback, (XtPointer)&info);
    }
}

static int
ResolveColumnNumber(TextWidget ctx)
{
    Widget src = ctx->text.source;
    short column_number = 0;
    XawTextPosition position;
    XawTextBlock block;
    unsigned long format = _XawTextFormat(ctx);
    TextSinkObject sink = (TextSinkObject)ctx->text.sink;
    short *char_tabs = sink->text_sink.char_tabs;
    int tab_count = sink->text_sink.tab_count;
    int tab_index = 0, tab_column = 0, tab_base = 0;

    if (ctx->text.lt.base_line < 1)
	return (ctx->text.column_number);

    position = SrcScan(src, ctx->text.insertPos, XawstEOL, XawsdLeft, 1, False);
    XawTextSourceRead(src, position, &block, ctx->text.insertPos - position);

    for (; position < ctx->text.insertPos; position++) {
	if (position - block.firstPos >= block.length)
	    XawTextSourceRead(src, position, &block, ctx->text.insertPos - position);
	if ((format == XawFmt8Bit && block.ptr[position - block.firstPos] == '\t') ||
	    (format == XawFmtWide && ((wchar_t*)block.ptr)[position - block.firstPos] == _Xaw_atowc(XawTAB))) {
	    while (tab_base + tab_column <= column_number) {
		if (tab_count) {
		    for (; tab_index < tab_count; ++tab_index)
			if (tab_base + char_tabs[tab_index] > column_number) {
			    tab_column = char_tabs[tab_index];
			    break;
			}
		    if (tab_index >= tab_count) {
			tab_base += char_tabs[tab_count - 1];
			tab_column = tab_index = 0;
		    }
		}
		else
		    tab_column += DEFAULT_TAB_SIZE;
	    }
	    column_number = tab_base + tab_column;
	}
	else
	    ++column_number;
	if (column_number >= 16384) {
	    column_number = 16383;
	    break;
	}
    }

    return (column_number);
}
#endif /* OLDXAW */

void
_XawTextSourceChanged(Widget w, XawTextPosition left, XawTextPosition right,
		      XawTextBlock *block, int lines)
{
    TextWidget ctx = (TextWidget)w;
    Widget src = ctx->text.source;
    XawTextPosition update_from, update_to, top;
    Boolean update_disabled;
    int delta, line, line_from;

    if (left < ctx->text.old_insert) {
	XawTextPosition old_insert = ctx->text.old_insert;

	if (right < ctx->text.old_insert)
	    old_insert -= right - left;
	else
	    old_insert = left;

	ctx->text.insertPos = old_insert + block->length;
    }
#ifndef OLDXAW
    if (left <= ctx->text.lt.top) {
	if (left + block->length - (right - left) < ctx->text.lt.top) {
	    ctx->text.source_changed = SRC_CHANGE_BEFORE;
	    ctx->text.lt.base_line += lines;
	}
	else
	    ctx->text.source_changed = SRC_CHANGE_OVERLAP;
    }
    else
	ctx->text.source_changed = SRC_CHANGE_AFTER;
#endif

    update_from = left;
    update_to = left + block->length;
    update_to = SrcScan(src, update_to, XawstEOL, XawsdRight, 1, False);
    delta = block->length - (right - left);
    if (delta < 0)
	ctx->text.clear_to_eol = True;
    if (update_to == update_from)
	++update_to;
    update_disabled = ctx->text.update_disabled;
    ctx->text.update_disabled = True;
    ctx->text.lastPos = XawTextGetLastPosition(ctx);
    top = ctx->text.lt.info[0].position;

    XawTextUnsetSelection((Widget)ctx);

    if (delta) {
	int i;
	XmuSegment *seg;

	for (seg = ctx->text.update->segment; seg; seg = seg->next) {
	    if (seg->x1 > (int)left)
		break;
	    else if (seg->x2 > (int)left) {
		seg->x2 += delta;
		seg = seg->next;
		break;
	    }
	}
	for (; seg; seg = seg->next) {
	    seg->x1 += delta;
	    seg->x2 += delta;
	}
	XmuOptimizeScanline(ctx->text.update);

	for (i = 0; i <= ctx->text.lt.lines; i++)
	    if (ctx->text.lt.info[i].position > left)
		break;
	for (; i <= ctx->text.lt.lines; i++)
	    ctx->text.lt.info[i].position += delta;
    }

    if (top != ctx->text.lt.info[0].position) {
	line_from = line = 0;
	ctx->text.lt.top = top = SrcScan(src, ctx->text.lt.info[0].position,
					 XawstEOL, XawsdLeft, 1, False);
	update_from = top;
    }
    else {
	line_from = line = LineForPosition(ctx, update_from + delta);
	top = ctx->text.lt.info[line].position;
    }

    if (line > 0 && ctx->text.wrap == XawtextWrapWord) {
	--line;
	top = ctx->text.lt.info[line].position;
    }

    (void)_BuildLineTable(ctx, top, line);

    if (ctx->text.wrap == XawtextWrapWord) {
	if (line_from != LineForPosition(ctx, update_from)
	    || line_from != LineForPosition(ctx, update_to)) {
	    ctx->text.clear_to_eol = True;
	    update_from = SrcScan(src, update_from,
				  XawstWhiteSpace, XawsdLeft, 1, True);
	    if (update_to >= ctx->text.lastPos)
	    /* this is not an error, it just tells _BuildLineTable to
	     * clear to the bottom of the window. The value of update_to
	     * should not be > ctx->text.lastPos.
	     */
		++update_to;
	}
    }
    else if (!ctx->text.clear_to_eol) {
	if (LineForPosition(ctx, update_from)
	    != LineForPosition(ctx, update_to))
	    ctx->text.clear_to_eol = True;
    }

    _XawTextNeedsUpdating(ctx, update_from, update_to);
    ctx->text.update_disabled = update_disabled;
}

/*
 * Function:
 *	_XawTextReplace
 *
 * Parameters:
 *	ctx   - text widget
 *	left  - left offset
 *	right - right offset
 *	block - text block
 *
 * Description:
 *	  Replaces the text between left and right by the text in block.
 *	  Does all the required calculations of offsets, and rebuild the
 *	the line table, from the insertion point (or previous line, if
 *	wrap mode is 'word').
 *
 * Returns:
 *	XawEditDone     - success
 *	any other value - error code
 */
int
_XawTextReplace(TextWidget ctx, XawTextPosition left, XawTextPosition right,
		XawTextBlock *block)
{
    Arg args[1];
    Widget src;
    XawTextEditType edit_mode;

    if (left == right && block->length == 0)
	return (XawEditDone);

    src = ctx->text.source;
    XtSetArg(args[0], XtNeditType, &edit_mode);
    XtGetValues(src, args, 1);

    if (edit_mode == XawtextAppend) {
	if (block->length == 0)
	    return (XawEditError);
	ctx->text.insertPos = ctx->text.lastPos;
    }

#ifndef OLDXAW
    return (SrcReplace(src, left, right, block));
#else
    if (SrcReplace(src, left, right, block) == XawEditDone) {
	_XawTextSourceChanged((Widget)ctx, left, right, block, 0);

	return (XawEditDone);
    }

    return (XawEditError);
#endif
}

/*
 * This routine will display text between two arbitrary source positions.
 * In the event that this span contains highlighted text for the selection,
 * only that portion will be displayed highlighted.
 */
static void
OldDisplayText(Widget w, XawTextPosition left, XawTextPosition right)
{
    static XmuSegment segment;
    static XmuScanline next;
    static XmuScanline scanline = {0, &segment, &next};
    static XmuArea area = {&scanline};

    TextWidget ctx = (TextWidget)w;
    int x, y, line;
    XawTextPosition start, end, last, final;
    XmuScanline *scan;
    XmuSegment *seg;
    XmuArea *clip = NULL;
    Bool cleol = ctx->text.clear_to_eol;
    Bool has_selection = ctx->text.s.right > ctx->text.s.left;

    left = left < ctx->text.lt.top ? ctx->text.lt.top : left;

    if (left > right || !LineAndXYForPosition(ctx, left, &line, &x, &y))
	return;

    last = XawTextGetLastPosition(ctx);
    segment.x2 = (int)XtWidth(ctx) - ctx->text.r_margin.right;

    if (cleol)
	clip = XmuCreateArea();

    for (start = left; start < right && line < ctx->text.lt.lines; line++) {
	if ((end = ctx->text.lt.info[line + 1].position) > right)
	    end = right;

	final = end;
	if (end > last)
	    end = last;

	if (end > start) {
	    if (!has_selection
		|| (start >= ctx->text.s.right || end <= ctx->text.s.left))
		_XawTextSinkDisplayText(ctx->text.sink, x, y, start, end, False);
	    else if (start >= ctx->text.s.left && end <= ctx->text.s.right)
		_XawTextSinkDisplayText(ctx->text.sink, x, y, start, end, True);
	    else {
		OldDisplayText(w, start, ctx->text.s.left);
		OldDisplayText(w, Max(start, ctx->text.s.left),
			       Min(end, ctx->text.s.right));
		OldDisplayText(w, ctx->text.s.right, end);
	    }
	}

	x = ctx->text.left_margin;
	if (cleol) {
	    segment.x1 = ctx->text.lt.info[line].textWidth + x;
	    if (XmuValidSegment(&segment)) {
		scanline.y = y;
		next.y = ctx->text.lt.info[line + 1].y;
		XmuAreaOr(clip, &area);
	    }
	}

	start = final;
	y = ctx->text.lt.info[line + 1].y;
    }

    if (cleol)  {
	for (scan = clip->scanline; scan && scan->next; scan = scan->next)
	    for (seg = scan->segment; seg; seg = seg->next)
		SinkClearToBG(ctx->text.sink,
			      seg->x1, scan->y,
			      seg->x2 - seg->x1, scan->next->y - scan->y);
	XmuDestroyArea(clip);
    }
}

#ifndef OLDXAW
/*ARGSUSED*/
static void
DisplayText(Widget w, XawTextPosition left, XawTextPosition right)
{
    static XmuSegment segment;
    static XmuScanline next;
    static XmuScanline scanline = {0, &segment, &next};
    static XmuArea area = {&scanline};

    TextWidget ctx = (TextWidget)w;
    int y, line;
    XawTextPosition from, to, lastPos;
    Bool cleol = ctx->text.clear_to_eol;
    Bool has_selection = ctx->text.s.right > ctx->text.s.left;
    XawTextPaintList *paint_list;

    left = left < ctx->text.lt.top ? ctx->text.lt.top : left;

    if (left > right || !IsPositionVisible(ctx, left))
	return;

    line = LineForPosition(ctx, left);
    y = ctx->text.lt.info[line].y;
    segment.x2 = (int)XtWidth(ctx) - ctx->text.r_margin.right;
    lastPos = XawTextGetLastPosition(ctx);

    paint_list = ((TextSinkObject)ctx->text.sink)->text_sink.paint;

    for (from = left; from < right && line < ctx->text.lt.lines; line++) {
	if ((to = ctx->text.lt.info[line + 1].position) > right)
	    to = right;

	if (to > lastPos)
	    to = lastPos;

	if (from < to) {
	    if (!has_selection
		|| (from >= ctx->text.s.right || to <= ctx->text.s.left))
		XawTextSinkPreparePaint(ctx->text.sink, y, line, from, to, False);
	    else if (from >= ctx->text.s.left && to <= ctx->text.s.right)
		XawTextSinkPreparePaint(ctx->text.sink, y, line, from, to, True);
	    else {
		XawTextSinkPreparePaint(ctx->text.sink, y, line, from,
					ctx->text.s.left, False);
		XawTextSinkPreparePaint(ctx->text.sink, y, line,
					XawMax(from, ctx->text.s.left),
					XawMin(to, ctx->text.s.right), True);
		XawTextSinkPreparePaint(ctx->text.sink, y, line,
					ctx->text.s.right, to, False);
	    }
	}

	if (cleol) {
	    segment.x1 = ctx->text.lt.info[line].textWidth + ctx->text.left_margin;
	    if (XmuValidSegment(&segment)) {
		scanline.y = y;
		next.y = ctx->text.lt.info[line + 1].y;
		XmuAreaOr(paint_list->clip, &area);
	    }
	}
	y = ctx->text.lt.info[line + 1].y;
	from = to;
    }

    /* clear to the bottom of the window */
    if (cleol && line >= ctx->text.lt.lines) {
	segment.x1 = ctx->text.left_margin;
	if (XmuValidSegment(&segment)) {
	    scanline.y = y;
	    next.y = (int)XtHeight(ctx) - (int)ctx->text.margin.bottom;
	    XmuAreaOr(paint_list->clip, &area);
	}
    }
}
#endif

/*
 * This routine implements multi-click selection in a hardwired manner.
 * It supports multi-click entity cycling (char, word, line, file) and mouse
 * motion adjustment of the selected entitie (i.e. select a word then, with
 * button still down, adjust wich word you really meant by moving the mouse).
 * [NOTE: This routine is to be replaced by a set of procedures that
 * will allows clients to implements a wide class of draw through and
 * multi-click selection user interfaces.]
 */
static void
DoSelection(TextWidget ctx, XawTextPosition pos, Time time, Bool motion)
{
    XawTextPosition newLeft, newRight;
    XawTextSelectType newType, *sarray;
    Widget src = ctx->text.source;

    if (motion)
	newType = ctx->text.s.type;
    else {
	if ((labs((long) time - (long) ctx->text.lasttime) < MULTI_CLICK_TIME)
	    && (pos >= ctx->text.s.left && pos <= ctx->text.s.right)) {
	    sarray = ctx->text.sarray;
	    for (; *sarray != XawselectNull && *sarray != ctx->text.s.type;
		 sarray++)
		;
	    if (*sarray == XawselectNull)
		newType = *(ctx->text.sarray);
	    else {
		newType = *(sarray + 1);
		if (newType == XawselectNull)
		    newType = *(ctx->text.sarray);
	    }
	}
	else		/* single-click event */
	    newType = *(ctx->text.sarray);

	ctx->text.lasttime = time;
    }
    switch (newType) {
	case XawselectPosition:
	    newLeft = newRight = pos;
	    break;
	case XawselectChar:
	    newLeft = pos;
	    newRight = SrcScan(src, pos, XawstPositions, XawsdRight, 1, False);
	    break;
	case XawselectWord:
	case XawselectParagraph:
	case XawselectAlphaNumeric: {
	    XawTextScanType stype;

	    if (newType == XawselectWord)
		stype = XawstWhiteSpace;
	    else if (newType == XawselectParagraph)
		stype = XawstParagraph;
	    else
		stype = XawstAlphaNumeric;

	    /*
	     * Somewhat complicated, but basically I treat the space between
	     * two objects as another object.  The object that I am currently
	     * in then becomes the end of the selection.
	     *
	     * Chris Peterson - 4/19/90.
	     */
	    newRight = SrcScan(ctx->text.source, pos, stype,
			       XawsdRight, 1, False);
	    newRight = SrcScan(ctx->text.source, newRight, stype,
			       XawsdLeft, 1, False);

	    if (pos != newRight)
		newLeft = SrcScan(ctx->text.source, pos, stype,
				  XawsdLeft, 1, False);
	    else
		newLeft = pos;

	    newLeft =SrcScan(ctx->text.source, newLeft, stype,
			     XawsdRight, 1, False);

	    if (newLeft > newRight) {
		XawTextPosition temp = newLeft;
		newLeft = newRight;
		newRight = temp;
	    }
	}   break;
	case XawselectLine:
	    newLeft = SrcScan(src, pos, XawstEOL, XawsdLeft, 1, False);
	    newRight = SrcScan(src, pos, XawstEOL, XawsdRight, 1, False);
	    break;
	case XawselectAll:
	    newLeft = SrcScan(src, pos, XawstAll, XawsdLeft, 1, False);
	    newRight = SrcScan(src, pos, XawstAll, XawsdRight, 1, False);
	    break;
	default:
	    XtAppWarning(XtWidgetToApplicationContext((Widget) ctx),
			 "Text Widget: empty selection array.");
	    return;
    }

    if (newLeft != ctx->text.s.left || newRight != ctx->text.s.right
	|| newType != ctx->text.s.type) {
	ModifySelection(ctx, newLeft, newRight);
	if (pos - ctx->text.s.left < ctx->text.s.right - pos)
	    ctx->text.insertPos = newLeft;
	else
	    ctx->text.insertPos = newRight;
	ctx->text.s.type = newType;
    }
    if (!motion) {	/* setup so we can freely mix select extend calls*/
	ctx->text.origSel.type = ctx->text.s.type;
	ctx->text.origSel.left = ctx->text.s.left;
	ctx->text.origSel.right = ctx->text.s.right;

	if (pos >= ctx->text.s.left + (ctx->text.s.right - ctx->text.s.left) / 2)
	    ctx->text.extendDir = XawsdRight;
	else
	    ctx->text.extendDir = XawsdLeft;
    }
}

/*
 * This routine implements extension of the currently selected text in
 * the "current" mode (i.e. char word, line, etc.). It worries about
 * extending from either end of the selection and handles the case when you
 * cross through the "center" of the current selection (e.g. switch which
 * end you are extending!).
 */
static void
ExtendSelection(TextWidget ctx, XawTextPosition pos, Bool motion)
{
    XawTextScanDirection dir;

    if (!motion) {	/* setup for extending selection */
	if (ctx->text.s.left == ctx->text.s.right) /* no current selection. */
	    ctx->text.s.left = ctx->text.s.right = ctx->text.insertPos;
	else {
	    ctx->text.origSel.left = ctx->text.s.left;
	    ctx->text.origSel.right = ctx->text.s.right;
	}

	ctx->text.origSel.type = ctx->text.s.type;

	if (pos >= ctx->text.s.left + (ctx->text.s.right - ctx->text.s.left) / 2)
	    ctx->text.extendDir = XawsdRight;
	else
	    ctx->text.extendDir = XawsdLeft;
    }
    else	/* check for change in extend direction */
	if ((ctx->text.extendDir == XawsdRight &&
	     pos <= ctx->text.origSel.left) ||
	    (ctx->text.extendDir == XawsdLeft &&
	     pos >= ctx->text.origSel.right)) {
	    ctx->text.extendDir = (ctx->text.extendDir == XawsdRight) ?
		XawsdLeft : XawsdRight;
	    ModifySelection(ctx, ctx->text.origSel.left, ctx->text.origSel.right);
	}

    dir = ctx->text.extendDir;
    switch (ctx->text.s.type) {
	case XawselectWord:
	case XawselectParagraph:
	case XawselectAlphaNumeric: {
	    XawTextPosition left_pos, right_pos;
	    XawTextScanType stype;

	    if (ctx->text.s.type == XawselectWord)
	        stype = XawstWhiteSpace;
	    else if (ctx->text.s.type == XawselectParagraph)
	        stype = XawstParagraph;
	    else
	        stype = XawstAlphaNumeric;

	    /*
	     * Somewhat complicated, but basically I treat the space between
	     * two objects as another object.  The object that I am currently
	     * in then becomes the end of the selection.
	     *
	     * Chris Peterson - 4/19/90.
	     */
	    right_pos = SrcScan(ctx->text.source, pos, stype,
				XawsdRight, 1, False);
	    right_pos =SrcScan(ctx->text.source, right_pos, stype,
			       XawsdLeft, 1, False);

	    if (pos != right_pos)
		left_pos = SrcScan(ctx->text.source, pos, stype,
				   XawsdLeft, 1, False);
	    else
		left_pos = pos;

	    left_pos =SrcScan(ctx->text.source, left_pos, stype,
			      XawsdRight, 1, False);

	    if (dir == XawsdLeft)
		pos = Min(left_pos, right_pos);
	    else	/* dir == XawsdRight */
		pos = Max(left_pos, right_pos);
	}   break;
	case XawselectLine:
	    pos = SrcScan(ctx->text.source, pos, XawstEOL,
			  dir, 1, dir == XawsdRight);
	    break;
	case XawselectAll:
	    pos = ctx->text.insertPos;
	    /*FALLTHROUGH*/
	case XawselectPosition:
	default:
	    break;
    }

    if (dir == XawsdRight)
	ModifySelection(ctx, ctx->text.s.left, pos);
    else
	ModifySelection(ctx, pos, ctx->text.s.right);

    ctx->text.insertPos = pos;
}

/*
 * Function:
 *	_XawTextClearAndCenterDisplay
 *
 * Parameters:
 *	ctx - text widget
 *
 * Description:
 *	  Redraws the display with the cursor in insert point
 *		     centered vertically.
 */
void
_XawTextClearAndCenterDisplay(TextWidget ctx)
{
    int left_margin = ctx->text.left_margin;
    Bool visible = IsPositionVisible(ctx, ctx->text.insertPos);

    _XawTextShowPosition(ctx);

    if (XtIsRealized((Widget)ctx) && visible &&
	left_margin == ctx->text.left_margin) {
	int insert_line = LineForPosition(ctx, ctx->text.insertPos);
	int scroll_by = insert_line - (ctx->text.lt.lines >> 1);
	Boolean clear_to_eol = ctx->text.clear_to_eol;

	XawTextScroll(ctx, scroll_by, 0);
	SinkClearToBG(ctx->text.sink, 0, 0, XtWidth(ctx), XtHeight(ctx));
	ClearWindow(ctx);
	clear_to_eol = ctx->text.clear_to_eol;
	ctx->text.clear_to_eol = False;
	FlushUpdate(ctx);
	ctx->text.clear_to_eol = clear_to_eol;
    }
}

/*
 * Internal redisplay entire window
 * Legal to call only if widget is realized
 */
static void
DisplayTextWindow(Widget w)
{
    TextWidget ctx = (TextWidget)w;

    _XawTextBuildLineTable(ctx, ctx->text.lt.top, False);
    ClearWindow(ctx);
}

static void
TextSinkResize(Widget w)
{
    if (w && XtClass(w)->core_class.resize)
	XtClass(w)->core_class.resize(w);
}

/* ARGSUSED */
void
_XawTextCheckResize(TextWidget ctx)
{
    return;
}

/*
 * Converts (params, num_params) to a list of atoms & caches the
 * list in the TextWidget instance.
 */
Atom *
_XawTextSelectionList(TextWidget ctx, String *list, Cardinal nelems)
{
    Atom *sel = ctx->text.s.selections;
    Display *dpy = XtDisplay((Widget)ctx);
    int n;

    if (nelems > (Cardinal)ctx->text.s.array_size) {
	sel = (Atom *)XtRealloc((char *)sel, sizeof(Atom) * nelems);
	ctx->text.s.array_size = nelems;
	ctx->text.s.selections = sel;
    }
    for (n = nelems; --n >= 0; sel++, list++)
	*sel = XInternAtom(dpy, *list, False);
    ctx->text.s.atom_count = nelems;

    return (ctx->text.s.selections);
}

/*
 * Function:
 *	SetSelection
 *
 * Parameters:
 *	ctx	   - text widget
 *	defaultSel - default selection
 *	l	   - left and right ends of the selection
 *	r	   - ""
 *	list	   - the selection list (as strings).
 *	nelems	   - ""
 *
 * Description:
 *	Sets the current selection.
 *
 * Note:
 *	if (ctx->text.s.left >= ctx->text.s.right) then the selection is unset
 */
void
_XawTextSetSelection(TextWidget ctx, XawTextPosition l, XawTextPosition r,
		     String *list, Cardinal nelems)
{
    if (nelems == 1 && !strcmp (list[0], "none"))
	return;
    if (nelems == 0) {
	static String defaultSel = "PRIMARY";
	list = &defaultSel;
	nelems = 1;
    }
    _SetSelection(ctx, l, r, _XawTextSelectionList(ctx, list, nelems), nelems);
}

/*
 * Function:
 *	ModifySelection
 *
 * Parameters:
 *	ctx   - text widget
 *	left  - left and right ends of the selection
 *	right - ""
 *
 * Description:
 *	Modifies the current selection.
 *
 * Note:
 *	if (ctx->text.s.left >= ctx->text.s.right) then the selection is unset
 */
static void
ModifySelection(TextWidget ctx, XawTextPosition left, XawTextPosition right)
{
    if (left == right)
	ctx->text.insertPos = left;
    _SetSelection(ctx, left, right, NULL, 0);
}

/*
 * This routine is used to perform various selection functions. The goal is
 * to be able to specify all the more popular forms of draw-through and
 * multi-click selection user interfaces from the outside.
 */
void
_XawTextAlterSelection(TextWidget ctx, XawTextSelectionMode mode,
		       XawTextSelectionAction action, String *params,
		       Cardinal *num_params)
{
    XawTextPosition position;
    Boolean flag;

    /*
     * This flag is used by TextPop.c:DoReplace() to determine if the selection
     * is okay to use, or if it has been modified.
     */
    if (ctx->text.search != NULL)
	ctx->text.search->selection_changed = True;

    position = PositionForXY(ctx, (int) ctx->text.ev_x, (int) ctx->text.ev_y);

    flag = (action != XawactionStart);
    if (mode == XawsmTextSelect)
	DoSelection(ctx, position, ctx->text.time, flag);
    else		/* mode == XawsmTextExtend */
	ExtendSelection (ctx, position, flag);

    if (action == XawactionEnd)
	_XawTextSetSelection(ctx, ctx->text.s.left, ctx->text.s.right,
			     params, *num_params);
}

/*
 * Function:
 *	UpdateTextInRectangle
 *
 * Parameters:
 *	ctx  - the text widget
 *	rect - rectangle
 *
 * Description:
 *	Updates the text in the given rectangle
 */
static void
UpdateTextInRectangle(TextWidget ctx, XRectangle *rect)
{
    XawTextLineTable *lt;
    int line, y1, y2, x2;

    y1 = rect->y;
    y2 = y1 + rect->height;
    x2 = rect->x + rect->width;

    for (line = 0, lt = &ctx->text.lt; line < lt->lines; line++)
	if (lt->info[line + 1].y > y1)
	    break;
    for (; line <= lt->lines; line++) {
	if (lt->info[line].y > y2)
	    break;
	UpdateTextInLine(ctx, line, rect->x, x2);
    }
}

/*
 * This routine processes all "expose region" XEvents. In general, its job
 * is to the best job at minimal re-paint of the text, displayed in the
 * window, that it can.
 */
/* ARGSUSED */
static void
XawTextExpose(Widget w, XEvent *event, Region region)
{
    TextWidget ctx = (TextWidget)w;
    Boolean clear_to_eol;
    XRectangle expose;

    if (event->type == Expose) {
	expose.x = event->xexpose.x;
	expose.y = event->xexpose.y;
	expose.width = event->xexpose.width;
	expose.height = event->xexpose.height;
    }
    else if (event->type == GraphicsExpose) {
	expose.x = event->xgraphicsexpose.x;
	expose.y = event->xgraphicsexpose.y;
	expose.width = event->xgraphicsexpose.width;
	expose.height = event->xgraphicsexpose.height;
    }
    else
	return;

    _XawTextPrepareToUpdate(ctx);

    if (Superclass->core_class.expose)
	(*Superclass->core_class.expose)(w, event, region);

    clear_to_eol = ctx->text.clear_to_eol;
    ctx->text.clear_to_eol = False;

    UpdateTextInRectangle(ctx, &expose);
    XawTextSinkGetCursorBounds(ctx->text.sink, &expose);
    UpdateTextInRectangle(ctx, &expose);
    SinkClearToBG(ctx->text.sink, expose.x, expose.y,
		  expose.width, expose.height);
    _XawTextExecuteUpdate(ctx);
    ctx->text.clear_to_eol = clear_to_eol;
}

/*
 * This routine does all setup required to syncronize batched screen updates
 */
void
_XawTextPrepareToUpdate(TextWidget ctx)
{
    if (ctx->text.old_insert < 0) {
	InsertCursor((Widget)ctx, XawisOff);
	ctx->text.showposition = False;
	ctx->text.old_insert = ctx->text.insertPos;
	ctx->text.clear_to_eol = False;
#ifndef OLDXAW
	ctx->text.source_changed = SRC_CHANGE_NONE;
#endif
    }
}

/*
 * This is a private utility routine used by _XawTextExecuteUpdate. It
 * processes all the outstanding update requests and merges update
 * ranges where possible.
 */
static void
FlushUpdate(TextWidget ctx)
{
    XmuSegment *seg;
    void (*display_text)(Widget, XawTextPosition, XawTextPosition);

    if (XtIsRealized((Widget)ctx)) {
	ctx->text.s.right = XawMin(ctx->text.s.right, ctx->text.lastPos);
	ctx->text.s.left = XawMin(ctx->text.s.left, ctx->text.s.right);

#ifndef OLDXAW
	if (XawTextSinkBeginPaint(ctx->text.sink) == False)
#endif
	    display_text = OldDisplayText;
#ifndef OLDXAW
	else
	    display_text = DisplayText;
#endif
	for (seg = ctx->text.update->segment; seg; seg = seg->next)
	    (*display_text)((Widget)ctx,
			    (XawTextPosition)seg->x1,
			    (XawTextPosition)seg->x2);
#ifndef OLDXAW
	if (display_text != OldDisplayText) {
	    XawTextSinkDoPaint(ctx->text.sink);
	    XawTextSinkEndPaint(ctx->text.sink);
	}
#endif
    }
    (void)XmuScanlineXor(ctx->text.update, ctx->text.update);
}

static int
CountLines(TextWidget ctx, XawTextPosition left, XawTextPosition right)
{
    if (ctx->text.wrap == XawtextWrapNever || left >= right)
	return (1);
    else {
	XawTextPosition tmp;
	int dim, lines = 0, wwidth = GetMaxTextWidth(ctx);

	while (left < right) {
	    tmp = left;
	    XawTextSinkFindPosition(ctx->text.sink, left,
				    ctx->text.left_margin,
				    wwidth, ctx->text.wrap == XawtextWrapWord,
				    &left, &dim, &dim);
	    ++lines;
	    if (tmp == left)
	      ++left;
	}

	return (lines);
    }
    /*NOTREACHED*/
}

static int
GetMaxTextWidth(TextWidget ctx)
{
    XRectangle cursor;
    int width;

    XawTextSinkGetCursorBounds(ctx->text.sink, &cursor);
    width = (int)XtWidth(ctx) - RHMargins(ctx) - cursor.width;

    return (XawMax(0, width));
}

/*
 * Function:
 *	_XawTextShowPosition
 *
 * Parameters:
 *	ctx - the text widget to show the position
 *
 * Description:
 *	  Makes sure the text cursor visible, scrolling the text window
 *	if required.
 */
void
_XawTextShowPosition(TextWidget ctx)
{
    /*
     * Variable scroll is used to avoid scanning large files to calculate
     * line offsets
     */
    int hpixels, vlines;
    XawTextPosition first, last, top, tmp;
    Bool visible, scroll;

    if (!XtIsRealized((Widget)ctx))
	return;

    /*
     * Checks if a horizontal scroll is required
     */
    if (ctx->text.wrap == XawtextWrapNever) {
	int x, vwidth, distance, dim;
	XRectangle rect;

	vwidth = (int)XtWidth(ctx) - RHMargins(ctx);
	last = SrcScan(ctx->text.source, ctx->text.insertPos,
		       XawstEOL, XawsdLeft, 1, False);
	XawTextSinkFindDistance(ctx->text.sink, last,
				ctx->text.left_margin,
				ctx->text.insertPos,
				&distance, &first, &dim);
	XawTextSinkGetCursorBounds(ctx->text.sink, &rect);
	x = ctx->text.left_margin - ctx->text.r_margin.left;

	if (x + distance + rect.width > vwidth)
	    hpixels = x + distance + rect.width - vwidth + (vwidth >> 2);
	else if (x + distance < 0)
	    hpixels = x + distance - (vwidth >> 2);
	else
	    hpixels = 0;
    }
    else
	hpixels = 0;

    visible = IsPositionVisible(ctx, ctx->text.insertPos);

    /*
     * If the cursor is already visible
     */
    if (!hpixels && visible)
	return;

    scroll = ctx->core.background_pixmap == XtUnspecifiedPixmap && !hpixels;
    vlines = 0;
    first = ctx->text.lt.top;

    /*
     * Needs to scroll the text window
     */
    if (visible)
      top = ctx->text.lt.top;
    else {
        top = SrcScan(ctx->text.source, ctx->text.insertPos,
		      XawstEOL, XawsdLeft, 1, False);

	/*
	 * Finds the nearest left position from ctx->text.insertPos
	 */
	if (ctx->text.wrap != XawtextWrapNever) {
	    int dim, vwidth = GetMaxTextWidth(ctx);

	    last = top;
	    /*CONSTCOND*/
	    while (1) {
		tmp = last;
		XawTextSinkFindPosition(ctx->text.sink, last,
					ctx->text.left_margin, vwidth,
					ctx->text.wrap == XawtextWrapWord,
					&last, &dim, &dim);
		if (last == tmp)
		    ++last;
		if (last <= ctx->text.insertPos)
		    top = last;
		else
		    break;
	    }
	}
    }

    if (scroll) {
	if (ctx->text.insertPos < first) {	/* Scroll Down */
	    while (first > top) {
		last = first;
		first = SrcScan(ctx->text.source, first,
				XawstEOL, XawsdLeft, 2, False);
		vlines -= CountLines(ctx, first, last);
		if (-vlines >= ctx->text.lt.lines) {
		    scroll = False;
		    break;
		}
	    }
	}
	else if (!visible) {			/* Scroll Up */
	    while (first < top) {
		last = first;
		first = SrcScan(ctx->text.source, first,
				XawstEOL, XawsdRight, 1, True);
		vlines += CountLines(ctx, last, first);
		if (vlines > ctx->text.lt.lines) {
		    scroll = False;
		    break;
		}
	    }
	}
	else
	    scroll = False;
    }

    /*
     * If a portion of the text that will be scrolled is visible
     */
    if (scroll)
	XawTextScroll(ctx, vlines ? vlines - (ctx->text.lt.lines >> 1) : 0, 0);
    /*
     * Else redraw the entire text window
     */
    else {
	ctx->text.left_margin -= hpixels;
	if (ctx->text.left_margin > ctx->text.r_margin.left)
	    ctx->text.left_margin = ctx->text.margin.left =
		ctx->text.r_margin.left;

	if (!visible) {
	    vlines = ctx->text.lt.lines >> 1;
	    if (vlines)
		top = SrcScan(ctx->text.source, ctx->text.insertPos,
			      XawstEOL, XawsdLeft, vlines + 1, False);

	    if (ctx->text.wrap != XawtextWrapNever) {
		int dim;
		int n_lines = CountLines(ctx, top, ctx->text.insertPos);
		int vwidth = GetMaxTextWidth(ctx);

		while (n_lines-- > vlines) {
		    tmp = top;
		    XawTextSinkFindPosition(ctx->text.sink, top,
					    ctx->text.left_margin,
					    vwidth,
					    ctx->text.wrap == XawtextWrapWord,
					    &top, &dim, &dim);
		    if (tmp == top)
			++top;
		}
	    }
	    _XawTextBuildLineTable(ctx, top, True);
	}
	else
	    ClearWindow(ctx);
    }
    ctx->text.clear_to_eol = True;
}

#ifndef OLDXAW
static int
ResolveLineNumber(TextWidget ctx)
{
    int line_number = ctx->text.lt.base_line;
    XawTextPosition position = ctx->text.lt.top;

    if (ctx->text.lt.base_line < 1)
	return (ctx->text.line_number);

    if (ctx->text.wrap == XawtextWrapNever
	&& IsPositionVisible(ctx, ctx->text.insertPos))
	line_number += LineForPosition(ctx, ctx->text.insertPos);
    else if (position < ctx->text.insertPos) {
	while (position < ctx->text.insertPos) {
	    position = SrcScan(ctx->text.source, position,
			       XawstEOL, XawsdRight, 1, True);
	    if (position <= ctx->text.insertPos) {
		++line_number;
		if (position == ctx->text.lastPos) {
		    line_number -= !_XawTextSourceNewLineAtEOF(ctx->text.source);
		    break;
		}
	    }
	}
    }
    else if (position > ctx->text.insertPos) {
	while (position > ctx->text.insertPos) {
	    position = SrcScan(ctx->text.source, position,
			       XawstEOL, XawsdLeft, 1, False);
	    if (--position >= ctx->text.insertPos)
		--line_number;
	}
    }

    return (line_number);
}
#endif

/*
 * This routine causes all batched screen updates to be performed
 */
void
_XawTextExecuteUpdate(TextWidget ctx)
{
    if (ctx->text.update_disabled || ctx->text.old_insert < 0)
	return;

    if(ctx->text.old_insert != ctx->text.insertPos || ctx->text.showposition)
	_XawTextShowPosition(ctx);

    FlushUpdate(ctx);
    InsertCursor((Widget)ctx, XawisOn);
    ctx->text.old_insert = -1;
#ifndef OLDXAW
    _XawTextSetLineAndColumnNumber(ctx, False);
#endif
}

static void
XawTextDestroy(Widget w)
{
    TextWidget ctx = (TextWidget)w;

    DestroyHScrollBar(ctx);
    DestroyVScrollBar(ctx);

    XtFree((char *)ctx->text.s.selections);
    XtFree((char *)ctx->text.lt.info);
    XtFree((char *)ctx->text.search);
    XmuDestroyScanline(ctx->text.update);
    XtReleaseGC((Widget)ctx, ctx->text.gc);
}

/*
 * by the time we are managed (and get this far) we had better
 * have both a source and a sink
 */
static void
XawTextResize(Widget w)
{
    TextWidget ctx = (TextWidget)w;

    PositionVScrollBar(ctx);
    PositionHScrollBar(ctx);
    TextSinkResize(ctx->text.sink);

    ctx->text.showposition = True;
    _XawTextBuildLineTable(ctx, ctx->text.lt.top, True);
}

/*
 * This routine allow the application program to Set attributes.
 */
/*ARGSUSED*/
static Boolean
XawTextSetValues(Widget current, Widget request, Widget cnew,
		 ArgList args, Cardinal *num_args)
{
    TextWidget oldtw = (TextWidget)current;
    TextWidget newtw = (TextWidget)cnew;
    Boolean redisplay = False;
    Boolean display_caret = newtw->text.display_caret;
#ifndef OLDXAW
    Boolean show_lc = False;
#endif

    newtw->text.display_caret = oldtw->text.display_caret;
    _XawTextPrepareToUpdate(newtw);
    newtw->text.display_caret = display_caret;

    if (oldtw->text.r_margin.left != newtw->text.r_margin.left) {
	newtw->text.left_margin = newtw->text.margin.left =
	    newtw->text.r_margin.left;
	if (newtw->text.vbar != NULL) {
	    newtw->text.left_margin += XtWidth(newtw->text.vbar) +
		XtBorderWidth(newtw->text.vbar);
	}
	redisplay = True;
    }

    if (oldtw->text.scroll_vert != newtw->text.scroll_vert) {
	if (newtw->text.scroll_vert == XawtextScrollAlways)
	    CreateVScrollBar(newtw);
	else
	    DestroyVScrollBar(newtw);

	redisplay = True;
    }

    if (oldtw->text.r_margin.bottom != newtw->text.r_margin.bottom) {
	newtw->text.margin.bottom = newtw->text.r_margin.bottom;
	if (newtw->text.hbar != NULL)
	    newtw->text.margin.bottom += newtw->text.hbar->core.height +
		newtw->text.hbar->core.border_width;
	redisplay = True;
    }

    if (oldtw->text.scroll_horiz != newtw->text.scroll_horiz) {
	if (newtw->text.scroll_horiz == XawtextScrollAlways)
	    CreateHScrollBar(newtw);
	else
	    DestroyHScrollBar(newtw);

	redisplay = True;
    }

    if (oldtw->text.source != newtw->text.source) {
#ifndef OLDXAW
	show_lc = True;
	_XawSourceRemoveText(oldtw->text.source, cnew,
			     oldtw->text.source &&
			     XtParent(oldtw->text.source) == cnew);
	_XawSourceAddText(newtw->text.source, cnew);
#endif
	_XawTextSetSource((Widget)newtw, newtw->text.source, newtw->text.lt.top,
			  newtw->text.insertPos);
    }

    newtw->text.redisplay_needed = False;
    XtSetValues((Widget)newtw->text.source, args, *num_args);
    XtSetValues((Widget)newtw->text.sink, args, *num_args);

    if (oldtw->text.wrap != newtw->text.wrap
	|| oldtw->text.lt.top != newtw->text.lt.top
	|| oldtw->text.insertPos != newtw->text.insertPos
	|| oldtw->text.r_margin.right != newtw->text.r_margin.right
	|| oldtw->text.r_margin.top != newtw->text.r_margin.top
	|| oldtw->text.sink != newtw->text.sink
	|| newtw->text.redisplay_needed) {
	if (oldtw->text.wrap != newtw->text.wrap) {
	    newtw->text.left_margin = newtw->text.margin.left =
		newtw->text.r_margin.left;
	    if (oldtw->text.lt.top == newtw->text.lt.top)
		newtw->text.lt.top = SrcScan(newtw->text.source, 0, XawstEOL,
					     XawsdLeft, 1, False);
	}
	newtw->text.showposition = True;
#ifndef OLDXAW
	show_lc = True;
	newtw->text.source_changed = SRC_CHANGE_OVERLAP;
#endif
	_XawTextBuildLineTable(newtw, newtw->text.lt.top, True);
	redisplay = True;
    }

#ifndef OLDXAW
    if (newtw->text.left_column < 0)
	newtw->text.left_column = 0;
    if (newtw->text.right_column < 0)
	newtw->text.right_column = 0;
#endif

    _XawTextExecuteUpdate(newtw);

#ifndef OLDXAW
    if (show_lc)
	_XawTextSetLineAndColumnNumber(newtw, True);
#endif

    if (redisplay)
	_XawTextSetScrollBars(newtw);

    return (redisplay);
}

/* invoked by the Simple widget's SetValues */
static Bool
XawTextChangeSensitive(Widget w)
{
    Arg args[1];
    TextWidget tw = (TextWidget)w;

    (*(&simpleClassRec)->simple_class.change_sensitive)(w);

    XtSetArg(args[0], XtNancestorSensitive,
	     (tw->core.ancestor_sensitive && tw->core.sensitive));
    if (tw->text.vbar)
	XtSetValues(tw->text.vbar, args, ONE);
    if (tw->text.hbar)
	XtSetValues(tw->text.hbar, args, ONE);
    return (False);
}

/*
 * Function:
 *	XawTextGetValuesHook
 *
 * Parameters:
 *	w	 - Text Widget
 *	args	 - argument list
 *	num_args - number of args
 *
 * Description:
 *	  This is a get values hook routine that gets the
 *		     values in the text source and sink.
 */
static void
XawTextGetValuesHook(Widget w, ArgList args, Cardinal *num_args)
{
    XtGetValues(((TextWidget)w)->text.source, args, *num_args);
    XtGetValues(((TextWidget)w)->text.sink, args, *num_args);
}

/*
 * Function:
 *	FindGoodPosition
 *
 * Parameters:
 *	pos - any position
 *
 * Description:
 *	Returns a valid position given any postition.
 *
 * Returns:
 *	A position between (0 and lastPos)
 */
static XawTextPosition
FindGoodPosition(TextWidget ctx, XawTextPosition pos)
{
    if (pos < 0)
	return (0);
    return (((pos > ctx->text.lastPos) ? ctx->text.lastPos : pos));
}

/* Li wrote this so the IM can find a given text position's screen position */
void
_XawTextPosToXY(Widget w, XawTextPosition pos, Position *x, Position *y)
{
    int line, ix, iy;

    LineAndXYForPosition((TextWidget)w, pos, &line, &ix, &iy);
    *x = ix;
    *y = iy;
}

/*******************************************************************
The following routines provide procedural interfaces to Text window state
setting and getting. They need to be redone so than the args code can use
them. I suggest we create a complete set that takes the context as an
argument and then have the public version lookup the context and call the
internal one. The major value of this set is that they have actual application
clients and therefore the functionality provided is required for any future
version of Text.
********************************************************************/
void
XawTextDisplay(Widget w)
{
    TextWidget ctx = (TextWidget)w;

    if (!XtIsRealized(w))
	return;

    _XawTextPrepareToUpdate(ctx);
    ctx->text.clear_to_eol = True;
    DisplayTextWindow(w);
    _XawTextExecuteUpdate(ctx);
}

void
XawTextSetSelectionArray(Widget w, XawTextSelectType *sarray)
{
    ((TextWidget)w)->text.sarray = sarray;
}

void
XawTextGetSelectionPos(Widget w, XawTextPosition *left, XawTextPosition *right)
{
    *left = ((TextWidget)w)->text.s.left;
    *right = ((TextWidget)w)->text.s.right;
}

void
_XawTextSetSource(Widget w, Widget source,
		  XawTextPosition top, XawTextPosition startPos)
{
    TextWidget ctx = (TextWidget)w;
#ifndef OLDXAW
    Bool resolve = False;
#endif

#ifndef OLDXAW
    if (source != ctx->text.source)
	_XawSourceRemoveText(ctx->text.source, w, ctx->text.source &&
			     XtParent(ctx->text.source) == w);
    _XawSourceAddText(source, w);

    if (source != ctx->text.source || ctx->text.insertPos != startPos)
	resolve = True;

    ctx->text.source_changed = SRC_CHANGE_OVERLAP;
#endif
    ctx->text.source = source;
    ctx->text.s.left = ctx->text.s.right = 0;
    ctx->text.lastPos = GETLASTPOS;
    top = FindGoodPosition(ctx, top);
    startPos = FindGoodPosition(ctx, startPos);
    ctx->text.insertPos = ctx->text.old_insert = startPos;
    _XawTextPrepareToUpdate(ctx);

    _XawTextBuildLineTable(ctx, top, True);

    _XawTextExecuteUpdate(ctx);
#ifndef OLDXAW
    if (resolve)
	_XawTextSetLineAndColumnNumber(ctx, True);
#endif
}

void
XawTextSetSource(Widget w, Widget source, XawTextPosition top)
{
    _XawTextSetSource(w, source, top, top);
}

/*
 * This public routine deletes the text from startPos to endPos in a source and
 * then inserts, at startPos, the text that was passed. As a side effect it
 * "invalidates" that portion of the displayed text (if any), so that things
 * will be repainted properly.
 */
int
XawTextReplace(Widget w, XawTextPosition startPos, XawTextPosition endPos,
	       XawTextBlock *text)
{
    TextWidget ctx = (TextWidget)w;
    int result;
#ifndef OLDXAW
    Cardinal i;
    TextSrcObject src = (TextSrcObject)ctx->text.source;

    for (i = 0; i < src->textSrc.num_text; i++)
	_XawTextPrepareToUpdate((TextWidget)src->textSrc.text[i]);
#else
    _XawTextPrepareToUpdate(ctx);
#endif

    endPos = FindGoodPosition(ctx, endPos);
    startPos = FindGoodPosition(ctx, startPos);
    result = _XawTextReplace(ctx, startPos, endPos, text);

#ifndef OLDXAW
    for (i = 0; i < src->textSrc.num_text; i++)
	_XawTextExecuteUpdate((TextWidget)src->textSrc.text[i]);
#else
    _XawTextExecuteUpdate(ctx);
#endif

    return (result);
}

XawTextPosition
XawTextTopPosition(Widget w)
{
    return (((TextWidget)w)->text.lt.top);
}

XawTextPosition
XawTextLastPosition(Widget w)
{
    return (((TextWidget)w)->text.lastPos);
}

void
XawTextSetInsertionPoint(Widget w, XawTextPosition position)
{
    TextWidget ctx = (TextWidget)w;

    _XawTextPrepareToUpdate(ctx);
    ctx->text.insertPos = FindGoodPosition(ctx, position);
    ctx->text.showposition = True;
    ctx->text.from_left = -1;

    _XawTextExecuteUpdate(ctx);
#ifndef OLDXAW
    _XawTextSetLineAndColumnNumber(ctx, False);
#endif
}

XawTextPosition
XawTextGetInsertionPoint(Widget w)
{
    return (((TextWidget)w)->text.insertPos);
}

/*
 * Note: Must walk the selection list in opposite order from TextLoseSelection
 */
void
XawTextUnsetSelection(Widget w)
{
    TextWidget ctx = (TextWidget)w;

    while (ctx->text.s.atom_count != 0) {
	Atom sel = ctx->text.s.selections[ctx->text.s.atom_count - 1];

	if (sel != (Atom) 0) {
	    /*
	     * As selections are lost the atom_count will decrement
	     */
	    if (GetCutBufferNumber(sel) == NOT_A_CUT_BUFFER)
		XtDisownSelection(w, sel, ctx->text.time);
	    TextLoseSelection(w, &sel); /* In case this is a cut buffer, or
					   XtDisownSelection failed to call us */
	}
    }
}

void
XawTextSetSelection(Widget w, XawTextPosition left, XawTextPosition right)
{
    TextWidget ctx = (TextWidget)w;

    _XawTextPrepareToUpdate(ctx);
    _XawTextSetSelection(ctx, FindGoodPosition(ctx, left),
			 FindGoodPosition(ctx, right), NULL, 0);
    _XawTextExecuteUpdate(ctx);
}

void
XawTextInvalidate(Widget w, XawTextPosition from, XawTextPosition to)
{
    TextWidget ctx = (TextWidget)w;

    from = FindGoodPosition(ctx, from);
    to = FindGoodPosition(ctx, to);
    ctx->text.lastPos = GETLASTPOS;
    _XawTextPrepareToUpdate(ctx);
    _XawTextNeedsUpdating(ctx, from, to);
    _XawTextExecuteUpdate(ctx);
}

/*ARGSUSED*/
void
XawTextDisableRedisplay(Widget w)
{
    ((TextWidget)w)->text.update_disabled = True;
    _XawTextPrepareToUpdate((TextWidget)w);
}

void
XawTextEnableRedisplay(Widget w)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition lastPos;

    if (!ctx->text.update_disabled)
	return;

    ctx->text.update_disabled = False;
    lastPos = ctx->text.lastPos = GETLASTPOS;
    ctx->text.lt.top = FindGoodPosition(ctx, ctx->text.lt.top);
    ctx->text.insertPos = FindGoodPosition(ctx, ctx->text.insertPos);

    if (ctx->text.s.left > lastPos || ctx->text.s.right > lastPos)
	ctx->text.s.left = ctx->text.s.right = 0;

    _XawTextExecuteUpdate(ctx);
}

Widget
XawTextGetSource(Widget w)
{
    return (((TextWidget)w)->text.source);
}

Widget
XawTextGetSink(Widget w)
{
    return (((TextWidget)w)->text.sink);
}

void
XawTextDisplayCaret(Widget w,
#if NeedWidePrototypes
	int display_caret
#else
	Boolean display_caret
#endif
)
{
    TextWidget ctx = (TextWidget)w;

    if (XtIsRealized(w)) {
	_XawTextPrepareToUpdate(ctx);
	ctx->text.display_caret = display_caret;
	_XawTextExecuteUpdate(ctx);
    }
    else
	ctx->text.display_caret = display_caret;
}

/*
 * Function:
 *	XawTextSearch
 *
 * Parameters:
 *	w    - text widget
 *	dir  - direction to search
 *	text - text block containing info about the string to search for
 *
 * Description:
 *	Searches for the given text block.
 *
 * Returns:
 *	The position of the text found, or XawTextSearchError on an error
 */
XawTextPosition
XawTextSearch(Widget w,
#if NeedWidePrototypes
	int dir,
#else
	XawTextScanDirection dir,
#endif
	XawTextBlock *text)
{
    TextWidget ctx = (TextWidget)w;

    return (SrcSearch(ctx->text.source, ctx->text.insertPos, dir, text));
}

TextClassRec textClassRec = {
  /* core */
  {
    (WidgetClass)&simpleClassRec,	/* superclass */
    "Text",				/* class_name */
    sizeof(TextRec),			/* widget_size */
    XawTextClassInitialize,		/* class_initialize */
    NULL,				/* class_part_init */
    False,				/* class_inited */
    XawTextInitialize,			/* initialize */
    NULL,				/* initialize_hook */
    XawTextRealize,			/* realize */
    _XawTextActionsTable,		/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resource */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    XtExposeGraphicsExpose |		/* compress_exposure */
	XtExposeNoExpose,
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawTextDestroy,			/* destroy */
    XawTextResize,			/* resize */
    XawTextExpose,			/* expose */
    XawTextSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    XawTextGetValuesHook,		/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    _XawDefaultTextTranslations,	/* tm_table */
    XtInheritQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* simple */
  {
    XawTextChangeSensitive,		/* change_sensitive */
  },
  /* text */
  {
    NULL,				/* extension */
  }
};

WidgetClass textWidgetClass = (WidgetClass)&textClassRec;
