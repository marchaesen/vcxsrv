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
#include <stdlib.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/AsciiSinkP.h>
#include <X11/Xaw/AsciiSrcP.h>
#include <X11/Xaw/TextP.h>
#include "Private.h"

#ifdef GETLASTPOS
#undef GETLASTPOS		/* We will use our own GETLASTPOS */
#endif

#define GETLASTPOS	\
	XawTextSourceScan(source, 0, XawstAll, XawsdRight, 1, True)

/*
 * Class Methods
 */
static void XawAsciiSinkClassPartInitialize(WidgetClass);
static void XawAsciiSinkInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawAsciiSinkDestroy(Widget);
static void XawAsciiSinkResize(Widget);
static Boolean XawAsciiSinkSetValues(Widget, Widget, Widget,
				     ArgList, Cardinal*);
static int MaxLines(Widget, unsigned int);
static int MaxHeight(Widget, int);
static void SetTabs(Widget, int, short*);
static void DisplayText(Widget, int, int,
			XawTextPosition, XawTextPosition, Bool);
static void InsertCursor(Widget, int, int, XawTextInsertState);
static void FindPosition(Widget, XawTextPosition, int, int, Bool,
			 XawTextPosition*, int*, int*);
static void FindDistance(Widget, XawTextPosition, int, XawTextPosition, int*,
			 XawTextPosition*, int*);
static void Resolve(Widget, XawTextPosition, int, int, XawTextPosition*);
static void GetCursorBounds(Widget, XRectangle*);
#ifndef OLDXAW
static void AsciiPreparePaint(Widget, int, int,
			      XawTextPosition, XawTextPosition, Bool);
static void AsciiDoPaint(Widget);
#endif

/*
 * Prototypes
 */
static void GetGC(AsciiSinkObject);
static int CharWidth(AsciiSinkObject, XFontStruct*, int, unsigned int);
static unsigned int PaintText(Widget w, GC gc, int x, int y,
			      char *buf, int len, Bool);

/*
 * Defined in TextSink.c
 */
void _XawTextSinkClearToBackground(Widget, int, int, unsigned, unsigned);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(AsciiSinkRec, ascii_sink.field)
static XtResource resources[] = {
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
    XtNecho,
    XtCOutput,
    XtRBoolean,
    sizeof(Boolean),
    offset(echo),
    XtRImmediate,
    (XtPointer)True
  },
  {
    XtNdisplayNonprinting,
    XtCOutput,
    XtRBoolean,
    sizeof(Boolean),
    offset(display_nonprinting),
    XtRImmediate,
    (XtPointer)
    True
  },
};
#undef offset

#define Superclass	(&textSinkClassRec)
AsciiSinkClassRec asciiSinkClassRec = {
  /* object */
  {
    (WidgetClass)Superclass,	/* superclass */
    "AsciiSink",		/* class_name */
    sizeof(AsciiSinkRec),	/* widget_size */
    XawInitializeWidgetSet,	/* class_initialize */
    XawAsciiSinkClassPartInitialize,	/* class_part_initialize */
    False,			/* class_inited	*/
    XawAsciiSinkInitialize,	/* initialize */
    NULL,			/* initialize_hook */
    NULL,			/* obj1 */
    NULL,			/* obj2 */
    0,				/* obj3 */
    resources,			/* resources */
    XtNumber(resources),	/* num_resources */
    NULLQUARK,			/* xrm_class */
    False,			/* obj4 */
    False,			/* obj5 */
    False,			/* obj6 */
    False,			/* obj7 */
    XawAsciiSinkDestroy,	/* destroy */
    (XtProc)XawAsciiSinkResize,	/* obj8 */
    NULL,			/* obj9 */
    XawAsciiSinkSetValues,	/* set_values */
    NULL,			/* set_values_hook */
    NULL,			/* obj10 */
    NULL,			/* get_values_hook */
    NULL,			/* obj11 */
    XtVersion,			/* version */
    NULL,			/* callback_private */
    NULL,			/* obj12 */
    NULL,			/* obj13 */
    NULL,			/* obj14 */
    NULL,			/* extension */
  },
  /* text_sink */
  {
    DisplayText,		/* DisplayText */
    InsertCursor,		/* InsertCursor */
    XtInheritClearToBackground,	/* ClearToBackground */
    FindPosition,		/* FindPosition */
    FindDistance,		/* FindDistance */
    Resolve,			/* Resolve */
    MaxLines,			/* MaxLines */
    MaxHeight,			/* MaxHeight */
    SetTabs,			/* SetTabs */
    GetCursorBounds,		/* GetCursorBounds */
#ifndef OLDXAW
    NULL			/* extension */
#endif
  },
  /* ascii_sink */
  {
    NULL,			/* extension */
  }
};

WidgetClass asciiSinkObjectClass = (WidgetClass)&asciiSinkClassRec;

/*
 * Implementation
 */
static void
XawAsciiSinkClassPartInitialize(WidgetClass wc _X_UNUSED)
{
#ifndef OLDXAW
    AsciiSinkObjectClass cclass = (AsciiSinkObjectClass)wc;
    XrmQuark record_type = XrmPermStringToQuark("TextSink");
    TextSinkExt ext = cclass->text_sink_class.extension;

    while (ext) {
	if (ext->record_type == record_type &&
	    ext->version == 1) {
	    ext->PreparePaint = AsciiPreparePaint;
	    ext->DoPaint = AsciiDoPaint;
	    break;
	}
	ext = (TextSinkExt)ext->next_extension;
    }
    if (ext == NULL)
	XtError("TextSinkClass: cannot resolve extension.\n");
#endif
}

static int
CharWidth(AsciiSinkObject sink, XFontStruct *font, int x, unsigned int c)
{
    int width = 0;

    if (c == XawLF)
	return (0);

    if (c == XawTAB) {
	int i;
	Position *tab;

	width = x;
	/* Adjust for Left Margin */
	x -= ((TextWidget)XtParent((Widget)sink))->text.left_margin;

	i = 0;
	tab = sink->text_sink.tabs;
	/*CONSTCOND*/
	while (1) {
	    if (x >= 0 && x < *tab)
		return (*tab - x);
	    /* Start again */
	    if (++i >= sink->text_sink.tab_count) {
		x -= *tab;
		i = 0;
		tab = sink->text_sink.tabs;
		if (width == x)
		    return (0);
	    }
	    else
		++tab;
	}
	/*NOTREACHED*/
    }

    if ((c & 0177) < XawSP || c == 0177) {
	if (sink->ascii_sink.display_nonprinting) {
	    if (c > 0177) {
		width = CharWidth(sink, font, x, '\\');
		width += CharWidth(sink, font, x, ((c >> 6) & 7) + '0');
		width += CharWidth(sink, font, x, ((c >> 3) & 7) + '0');
		c = (c & 7) + '0';
	    }
	    else {
		width = CharWidth(sink, font, x, '^');
		if ((c |= 0100) == 0177)
		    c = '?';
	    }
	}
	else
	    c = XawSP;
    }

    if (font->per_char
	&& (c >= font->min_char_or_byte2 && c <= font->max_char_or_byte2))
	width += font->per_char[c - font->min_char_or_byte2].width;
    else
	width += font->min_bounds.width;

    return (width);
}

#ifndef OLDXAW
static int
GetTextWidth(TextWidget ctx, int current_width, XFontStruct *font,
	     XawTextPosition from, int length)
{
    int i, width = 0;
    XawTextBlock block;
    XawTextPosition pos = from;

    while (length > 0) {
	pos = XawTextSourceRead(ctx->text.source, from, &block, length);
	length = (int)(length - (pos - from));
	from = pos;
	for (i = 0; i < block.length; i++)
	    width += CharWidth((AsciiSinkObject)ctx->text.sink, font,
			       current_width + width,
			       (unsigned char)block.ptr[i]);
    }

    return (width);
}

static
void CalculateBearing(TextWidget ctx, XawTextPosition position, int x, int y,
		      int ascent, int descent, Bool highlight, Bool right)
{
/*
 * Sample case:
 *
 * lbearing|    width	 |rbearing
 *	   |		 |
 *	   |	       ####
 *	   |	     ### |
 *	   |	   ####  |
 *	   |	  ####	 |
 *	   |  ########## |
 *	   |	####	 |
 *	   |   ####	 |
 *	   |  ####	 |
 *	   | ####	 |
 *	   |###		 |
 *	 ####		 |
 *	   |		 |
 *
 */
    AsciiSinkObject sink = (AsciiSinkObject)ctx->text.sink;
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextProperty *property;
    XawTextPaintStruct *paint;
    XawTextBlock block;
    XFontStruct *font;

    property = NULL;
    if (XawTextSourceAnchorAndEntity(ctx->text.source, position,
				     &anchor, &entity) &&
	(property = XawTextSinkGetProperty((Widget)sink,
					   entity->property)) != NULL &&
	(property->mask & XAW_TPROP_FONT))
	font = property->font;
    else
	font = sink->ascii_sink.font;
    if (right) {
	if (font->max_bounds.rbearing > 0) {
	    int rbearing = font->max_bounds.rbearing - font->max_bounds.width;
	    unsigned char c;

	    (void)XawTextSourceRead(ctx->text.source, position, &block, 1);
	    c = *(unsigned char*)block.ptr;
	    if (c == '\t' || c == '\n')
		c = ' ';
	    else if ((c & 0177) < XawSP || c == 0177) {
		if (sink->ascii_sink.display_nonprinting)
		    c = (unsigned char)(c > 0177 ? (c & 7) + '0' : c + '@');
		else
		    c = ' ';
	    }
	    if (font->per_char &&
		(c >= font->min_char_or_byte2 && c <= font->max_char_or_byte2))
		rbearing = font->per_char[c - font->min_char_or_byte2].rbearing -
			   font->per_char[c - font->min_char_or_byte2].width;
	    if (rbearing > 0) {
		paint = XtNew(XawTextPaintStruct);
		paint->next = sink->text_sink.paint->bearings;
		sink->text_sink.paint->bearings = paint;
		paint->x = x - (paint->width = CharWidth(sink, font, 0, c));
		paint->y = y + ascent;
		paint->property = property;
		paint->max_ascent = ascent;
		paint->max_descent = descent;
		paint->backtabs = NULL;
		paint->highlight = (Boolean)highlight;
		paint->length = 1;
		paint->text = XtMalloc(1);
		paint->text[0] = (char)c;
	    }
	}
    }
    else {
	if (font->min_bounds.lbearing < 0) {
	    int lbearing = font->min_bounds.lbearing;
	    unsigned char c;

	    (void)XawTextSourceRead(ctx->text.source, position, &block, 1);
	    c = *(unsigned char*)block.ptr;
	    if (c == '\t' || c == '\n')
		c = ' ';
	    else if ((c & 0177) < XawSP || c == 0177) {
		if (sink->ascii_sink.display_nonprinting)
		    c = (unsigned char)(c > 0177 ? '\\' : c + '^');
		else
		    c = ' ';
	    }
	    if (font->per_char &&
		(c >= font->min_char_or_byte2 && c <= font->max_char_or_byte2))
		lbearing = font->per_char[c - font->min_char_or_byte2].lbearing;
	    if (lbearing < 0) {
		paint = XtNew(XawTextPaintStruct);
		paint->next = sink->text_sink.paint->bearings;
		sink->text_sink.paint->bearings = paint;
		paint->x = x;
		paint->width = -CharWidth(sink, font, 0, c);
		paint->y = y + ascent;
		paint->property = property;
		paint->max_ascent = ascent;
		paint->max_descent = descent;
		paint->backtabs = NULL;
		paint->highlight = (Boolean)highlight;
		paint->length = 1;
		paint->text = XtMalloc(1);
		paint->text[0] = (char)c;
	    }
	}
    }
}

static void
AsciiPreparePaint(Widget w, int y, int line,
		  XawTextPosition from, XawTextPosition to, Bool highlight)
{
    static XmuSegment segment;
    static XmuScanline next;
    static XmuScanline scanline = {0, &segment, &next};
    static XmuArea area = {&scanline};

    TextWidget ctx = (TextWidget)XtParent(w);
    AsciiSinkObject sink = (AsciiSinkObject)ctx->text.sink;
    XawTextPosition left, right, pos, pos2, tmp, length;
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextProperty *property;
    int i, ascent = 0, descent = 0, xl, xr, x = ctx->text.left_margin, bufsiz;
    XawTextBlock block;
    XFontStruct *font;
    XawTextPaintStruct *paint;

    if (!sink->ascii_sink.echo)
	return;

    /* pass 1: calculate ascent/descent values and x coordinate */
    /* XXX the MAX ascent/descent value should be in the line table XXX */
    /* XXX the x coordinate can be a parameter, but since it is required
	   to calculate the ascent/descent, do it here to avoid an extra
	   search in the entities */
    pos = tmp = left = ctx->text.lt.info[line].position;
    right = ctx->text.lt.info[line + 1].position;
    right = XawMin(right, ctx->text.lastPos + 1);
    while (pos < right) {
	if (XawTextSourceAnchorAndEntity(ctx->text.source, pos,
					 &anchor, &entity)) {
	    if ((property = XawTextSinkGetProperty((Widget)sink,
						   entity->property)) != NULL &&
		(property->mask & XAW_TPROP_FONT))
		font = property->font;
	    else
		font = sink->ascii_sink.font;
	    tmp = pos;
	    pos = anchor->position + entity->offset + entity->length;
	    if ((length = XawMin(from, pos) - tmp) > 0)
		x += GetTextWidth(ctx, x, font, tmp, (int)length);
	    ascent = XawMax(font->ascent, ascent);
	    descent = XawMax(font->descent, descent);
	}
	else if (anchor) {
	    ascent = XawMax(sink->ascii_sink.font->ascent, ascent);
	    descent = XawMax(sink->ascii_sink.font->descent, descent);
	    while (entity && pos < right) {
		tmp = pos;
		if ((pos = anchor->position + entity->offset) < tmp)
		    pos = tmp;
		else {
		    if ((length = XawMin(from, pos) - tmp) > 0) {
			x += GetTextWidth(ctx, x, sink->ascii_sink.font, tmp,
					  (int)length);
			tmp += length;
		    }
		    if (pos < right) {
			pos += entity->length;
			if ((property = XawTextSinkGetProperty((Widget)sink,
							       entity->property)) != NULL &&
			    (property->mask & XAW_TPROP_FONT))
			    font = property->font;
			else
			    font = sink->ascii_sink.font;
			if ((length = XawMin(from, pos) - tmp) > 0)
			    x += GetTextWidth(ctx, x, font, tmp, (int)length);
			ascent = XawMax(font->ascent, ascent);
			descent = XawMax(font->descent, descent);
		    }
		}
		entity = entity->next;
	    }

	    if (anchor->entities == NULL) {
		tmp = XawMin(pos, from);
		if ((length = from - tmp) > 0)
		    x += GetTextWidth(ctx, x, sink->ascii_sink.font, tmp, (int)length);
		break;
	    }
	}
	else {
	    tmp = XawMin(pos, from);
	    if ((length = from - tmp) > 0)
		x += GetTextWidth(ctx, x, sink->ascii_sink.font, tmp, (int)length);
	    ascent = XawMax(sink->ascii_sink.font->ascent, ascent);
	    descent = XawMax(sink->ascii_sink.font->descent, descent);
	    break;
	}
    }
    if (!ascent)
	ascent = sink->ascii_sink.font->ascent;
    if (!descent)
	descent = sink->ascii_sink.font->descent;

    xl = x;

    /* pass 2: feed the XawTextPaintStruct lists */
    pos = from;
    while (pos < to) {
	paint = XtNew(XawTextPaintStruct);
	paint->next = sink->text_sink.paint->paint;
	sink->text_sink.paint->paint = paint;
	paint->x = x;
	paint->y = y + ascent;
	paint->property = NULL;
	paint->max_ascent = ascent;
	paint->max_descent = descent;
	paint->backtabs = NULL;
	paint->highlight = (Boolean)highlight;

	tmp = pos;
	if (XawTextSourceAnchorAndEntity(ctx->text.source, pos,
					 &anchor, &entity)) {
	    pos = anchor->position + entity->offset + entity->length;
	    if ((paint->property = XawTextSinkGetProperty((Widget)sink,
						   entity->property)) != NULL &&
		(paint->property->mask & XAW_TPROP_FONT))
		font = paint->property->font;
	    else
		font = sink->ascii_sink.font;
	}
	else {
	    if (anchor) {
		while (entity && anchor->position + entity->offset < pos)
		    entity = entity->next;
		if (entity)
		    pos = anchor->position + entity->offset;
		else
		    pos = to;
	    }
	    else
		pos = to;
	    font = sink->ascii_sink.font;
	}
	pos = XawMin(pos, to);
	length = pos - tmp;

	paint->text = XtMalloc((Cardinal)(bufsiz = (int)(pos - tmp + 4)));
	paint->length = 0;
	segment.x1 = x;

	pos2 = tmp;
	while (length > 0) {
	    pos2 = XawTextSourceRead(ctx->text.source, tmp, &block, (int)length);
	    length = pos - pos2;
	    tmp = pos2;
	    for (i = 0; i < block.length; i++) {
		unsigned char c = (unsigned char)block.ptr[i];

		if ((paint->length + 4) > (unsigned)bufsiz)
		    paint->text = XtRealloc(paint->text, (Cardinal)(bufsiz += 32));
		paint->text[paint->length] = (char)c;
		if (c == '\n') {
		    x += CharWidth(sink, font, 0, ' ');
		    continue;
		}
		if (c == '\t') {
		    x += XTextWidth(font, paint->text, (int)paint->length);
		    segment.x2 = x + CharWidth(sink, font, x, '\t');

		    if (XmuValidSegment(&segment)) {
			if (!highlight && (paint->property &&
			    (paint->property->mask & XAW_TPROP_BACKGROUND))) {
			    if (ascent > font->ascent) {
				scanline.y = y;
				next.y = y + ascent - font->ascent;
				XmuAreaOr(sink->text_sink.paint->clip, &area);
			    }
			    if (descent >= font->descent) {
				scanline.y = y + ascent + font->descent;
				next.y = scanline.y + descent - font->descent + 1;
				XmuAreaOr(sink->text_sink.paint->clip, &area);
			    }
			    if (paint->backtabs == NULL)
				paint->backtabs = XmuCreateArea();
			    scanline.y = y + ascent - font->ascent;
			    next.y = y + ascent + font->descent;
			    XmuAreaOr(paint->backtabs, &area);
			}
			else {
			    scanline.y = y;
			    next.y = ctx->text.lt.info[line + 1].y;
			    if (highlight) {
				if (!sink->text_sink.paint->hightabs)
				    sink->text_sink.paint->hightabs =
					XmuCreateArea();
				XmuAreaOr(sink->text_sink.paint->hightabs, &area);
			    }
			    else
				XmuAreaOr(sink->text_sink.paint->clip, &area);
			}
		    }

		    paint->width = segment.x2 - segment.x1;
		    x = segment.x1 = segment.x2;

		    if (paint->length == 0) {
			paint->x = x;
			continue;
		    }
		    paint->text = XtRealloc(paint->text, paint->length);
		    property = paint->property;
		    paint = XtNew(XawTextPaintStruct);
		    paint->next = sink->text_sink.paint->paint;
		    sink->text_sink.paint->paint = paint;
		    paint->x = x;
		    paint->y = y + ascent;
		    paint->property = property;
		    paint->max_ascent = ascent;
		    paint->max_descent = descent;
		    paint->backtabs = NULL;
		    paint->highlight = (Boolean)highlight;
		    paint->text = XtMalloc((Cardinal)(bufsiz = (int)(pos - tmp - length +
						    block.length - i + 4)));
		    paint->length = 0;
		    continue;
		}
		if ((c & 0177) < XawSP || c == 0177) {
		    if (sink->ascii_sink.display_nonprinting) {
			if (c > 0177) {
			    paint->text[paint->length++] = '\\';
			    paint->text[paint->length++] = (char)(((c >> 6) & 7) + '0');
			    paint->text[paint->length++] = (char)(((c >> 3) & 7) + '0');
			    paint->text[paint->length] = (char)((c & 7) + '0');
			}
			else {
			    c |= 0100;
			    paint->text[paint->length++] = '^';
			    paint->text[paint->length] = (char)(c == 0177 ? '?' : c);
			}
		    }
		    else
			paint->text[paint->length] = ' ';
		}
		paint->length++;
	    }
	}

	x += XTextWidth(font, paint->text, (int)paint->length);
	segment.x2 = x;
	if (XmuValidSegment(&segment)) {
	    /* erase only what really is needed */
	    /*if (!highlight || (paint->property &&
		(paint->property->mask & XAW_TPROP_BACKGROUND))) {
		if (ascent > font->ascent) {
		    scanline.y = y;
		    next.y = y + ascent - font->ascent;
		    XmuAreaOr(sink->text_sink.paint->clip, &area);
		}
		if (descent > font->descent) {
		    scanline.y = y + ascent + font->descent;
		    next.y = scanline.y + descent - font->descent;
		    XmuAreaOr(sink->text_sink.paint->clip, &area);
		}
	    }
	    else*/ {
		scanline.y = y;
		next.y = ctx->text.lt.info[line + 1].y;
		XmuAreaOr(sink->text_sink.paint->clip, &area);
	    }
	}

	paint->width = x - segment.x1;
    }

    xr = x;

    /* pass 3: bearing clipping */
    if (left < from) {
	CalculateBearing(ctx, from - 1, xl, y, ascent, descent, highlight, True);
	if (ctx->text.s.left < ctx->text.s.right) {
	    if (ctx->text.s.right == from)
		CalculateBearing(ctx, from, xl, y, ascent, descent, True, False);
	    else if (ctx->text.s.left == from)
		CalculateBearing(ctx, from, xl, y, ascent, descent, False, False);
	}
    }
    right = XawMin(right, ctx->text.lastPos);
    if (right >= to && to > from) {
	if (to < right)
	    CalculateBearing(ctx, to, xr, y, ascent, descent, highlight, False);
	if (ctx->text.s.left < ctx->text.s.right) {
	    if (ctx->text.s.right == to)
		CalculateBearing(ctx, to - 1, xr, y, ascent, descent, False, True);
	    else if (ctx->text.s.left == to)
		CalculateBearing(ctx, to - 1, xr, y, ascent, descent, True, True);
	}
    }
}

static int
qcmp_paint_struct(_Xconst void *left, _Xconst void *right)
{
    return (int)((*(XawTextPaintStruct* _Xconst *)left)->property -
	         (*(XawTextPaintStruct* _Xconst *)right)->property);
}

static void
AsciiDoPaint(Widget w)
{
    TextWidget ctx = (TextWidget)XtParent(w);
    AsciiSinkObject sink = (AsciiSinkObject)ctx->text.sink;
    XmuScanline *scan;
    XmuSegment *seg;
    XawTextPaintList *list = sink->text_sink.paint;
#if 0
    XawTextPaintStruct *base, *head;
#endif
    XawTextPaintStruct *paint = list->paint;
    XawTextProperty *property;
    XFontStruct *font = NULL;
    XRectangle *rects;
    int n_rects, i_rects;
    GC gc;
    Bool highlight;
    XRectangle rect;
    int width, height, line_width = -1;
    XGCValues values;

    /* pass 1: clear clipping areas */
    /* XXX Don't use XDrawImageString because the font may be italic, and
	   will get incorrectly drawn. Probably, it could be a good idea to
	   check if this is the case, and do special processing. But this
	   will need to be checked if required. */
    for (scan = list->clip->scanline; scan && scan->next; scan = scan->next)
	for (seg = scan->segment; seg; seg = seg->next)
	    _XawTextSinkClearToBackground(ctx->text.sink,
					  seg->x1, scan->y,
					  (unsigned)(seg->x2 - seg->x1),
					  (unsigned)(scan->next->y - scan->y));

    /* pass 2: optimize drawing list to avoid too much GC change requests */
    /* XXX this assumes there will not exist entities drawn over other
	   entities. */
#if 0
    while (paint) {
	base = paint;
	head = paint->next;
	while (head) {
	    if (head->property == paint->property) {
		base->next = head->next;
		head->next = paint->next;
		paint->next = head;
		paint = head;
	    }
	    base = head;
	    head = head->next;
	}
	paint = paint->next;
    }
#endif
    if (paint && paint->next) {
	XawTextPaintStruct **paints;
	int i = 0, n_paints = 0;

	while (paint) {
	    paint = paint->next;
	    ++n_paints;
	}
	paints = (XawTextPaintStruct**)
	    XtMalloc((Cardinal)((size_t)n_paints * sizeof(XawTextPaintStruct)));
	paint = list->paint;
	while (paint) {
	    paints[i++] = paint;
	    paint = paint->next;
	}
	qsort((void*)paints, (size_t)n_paints, sizeof(XawTextPaintStruct*),
	      qcmp_paint_struct);
	list->paint = paints[0];
	for (i = 0; i < n_paints - 1; i++)
	    paints[i]->next = paints[i + 1];
	paints[i]->next = NULL;
	XtFree((XtPointer)paints);
    }

    /* pass 3: clip gc */
    gc = sink->ascii_sink.normgc;

    rect.x = ctx->text.r_margin.left;
    rect.y = ctx->text.r_margin.top;
    width = (int)XtWidth(ctx) - RHMargins(ctx);
    height = (int)XtHeight(ctx) - RVMargins(ctx);
    rect.width = (unsigned short)width;
    rect.height = (unsigned short)height;
    if (width >= 0 && height >= 0)
	XSetClipRectangles(XtDisplay((Widget)ctx), gc,
			   0, 0, &rect, 1, Unsorted);
    else
	XSetClipMask(XtDisplay((Widget)ctx), gc, None);

    /* pass 4: draw backgrounds */
    paint = list->paint;
    property = NULL;
    rects = NULL;
    i_rects = n_rects = 0;
    while (paint) {
	if (paint->property && (paint->property->mask & XAW_TPROP_BACKGROUND)) {
	    if (property != paint->property) {
		if (i_rects)
		    XFillRectangles(XtDisplay(ctx), XtWindow(ctx), gc,
				    rects, i_rects);
		i_rects = 0;
		property = paint->property;
		if (property->mask & XAW_TPROP_FONT)
		    font = property->font;
		else
		    font = sink->ascii_sink.font;
		XSetForeground(XtDisplay(ctx), gc, property->background);
	    }
	    if (i_rects <= n_rects)
		rects = (XRectangle*)
		    XtRealloc((XtPointer)rects,	(Cardinal)(sizeof(XRectangle) *
			      (size_t)++n_rects));
	    rects[i_rects].x = (short)paint->x;
	    rects[i_rects].y = (short)(paint->y - font->ascent);
	    rects[i_rects].width = (unsigned short)paint->width;
	    rects[i_rects++].height = (unsigned short)(font->ascent + font->descent);

	    if (paint->backtabs) {
		for (scan = paint->backtabs->scanline; scan && scan->next;
		     scan = scan->next)
		    for (seg = scan->segment; seg; seg = seg->next) {
			if (i_rects <= n_rects)
			    rects = (XRectangle*)
				XtRealloc((XtPointer)rects, (Cardinal)(sizeof(XRectangle) *
					  (size_t)++n_rects));
			rects[i_rects].x = (short)seg->x1;
			rects[i_rects].y = (short)scan->y;
			rects[i_rects].width = (unsigned short)(seg->x2 - seg->x1);
			rects[i_rects++].height = (unsigned short)(scan->next->y - scan->y);
		    }
	    }


	}
	paint = paint->next;
    }
    if (i_rects)
	XFillRectangles(XtDisplay(ctx), XtWindow(ctx), gc, rects, i_rects);

    paint = list->paint;
    i_rects = 0;
    while (paint) {
	if (paint->highlight) {
	    if (i_rects == 0)
		XSetForeground(XtDisplay(ctx), gc, sink->text_sink.cursor_color);
	    if (i_rects <= n_rects)
		rects = (XRectangle*)
		    XtRealloc((XtPointer)rects,	(Cardinal)(sizeof(XRectangle) *
			      (size_t)++n_rects));
	    rects[i_rects].x = (short)paint->x;
	    rects[i_rects].y = (short)(paint->y - paint->max_ascent);
	    rects[i_rects].width = (unsigned short)paint->width;
	    rects[i_rects++].height = (unsigned short)(paint->max_ascent + paint->max_descent + 1);
	}
	paint = paint->next;
    }
    if (list->hightabs) {
	for (scan = list->hightabs->scanline; scan && scan->next;
	     scan = scan->next)
	    for (seg = scan->segment; seg; seg = seg->next) {
		if (i_rects == 0)
		    XSetForeground(XtDisplay(ctx), gc,
				   sink->text_sink.cursor_color);
		if (i_rects <= n_rects)
		    rects = (XRectangle*)
			XtRealloc((XtPointer)rects, (Cardinal)(sizeof(XRectangle) *
				  (size_t)++n_rects));
		rects[i_rects].x = (short)seg->x1;
		rects[i_rects].y = (short)scan->y;
		rects[i_rects].width = (unsigned short)(seg->x2 - seg->x1);
		rects[i_rects++].height = (unsigned short)(scan->next->y - scan->y);
	    }
    }

    if (i_rects)
	XFillRectangles(XtDisplay(ctx), XtWindow(ctx), gc, rects, i_rects);
    if (rects)
	XtFree((XtPointer)rects);

    /* pass 5: draw text! */
    paint = list->paint;
    if (paint && (property = paint->property) == NULL) {
	font = sink->ascii_sink.font;
	XSetFont(XtDisplay(ctx), gc, font->fid);
	if (!paint->highlight)
	    XSetForeground(XtDisplay(ctx), gc, sink->text_sink.foreground);
    }
    else
	property = NULL;
    highlight = False;
    while (paint) {
	if (!highlight && paint->highlight)
	    XSetForeground(XtDisplay(ctx), gc, sink->text_sink.background);
	if (highlight || paint->highlight || paint->property != property) {
	    if (!paint->property || !(paint->property->mask & XAW_TPROP_FONT))
		font = sink->ascii_sink.font;
	    else
		font = paint->property->font;
	    XSetFont(XtDisplay(ctx), gc, font->fid);
	    if (!paint->highlight) {
		if (!paint->property ||
		    !(paint->property->mask & XAW_TPROP_FOREGROUND))
		    XSetForeground(XtDisplay(ctx), gc,
				   sink->text_sink.foreground);
		else
		    XSetForeground(XtDisplay(ctx), gc,
				   paint->property->foreground);
	    }
	    highlight = paint->highlight;
	    property = paint->property;
	}

	if (paint->x < XtWidth(ctx) && paint->x + paint->width > 0) {
	    XDrawString(XtDisplay(ctx), XtWindow(ctx), gc, paint->x, paint->y,
			paint->text, (int)paint->length);
	    if (property) {
		if (property->mask & XAW_TPROP_UNDERLINE) {
		    if (line_width != property->underline_thickness) {
			values.line_width = line_width =
			    property->underline_thickness;
			XChangeGC(XtDisplay(ctx), gc, GCLineWidth, &values);
		    }

		    XDrawLine(XtDisplay(ctx), XtWindow(ctx), gc, paint->x,
			      paint->y + property->underline_position,
			      paint->x + paint->width,
			      paint->y + property->underline_position);
		}
		if (property->mask & XAW_TPROP_OVERSTRIKE) {
		    if (line_width != property->underline_thickness) {
			values.line_width = line_width =
			    property->underline_thickness;
			XChangeGC(XtDisplay(ctx), gc, GCLineWidth, &values);
		    }

		    XDrawLine(XtDisplay(ctx), XtWindow(ctx), gc, paint->x,
			      paint->y - (font->ascent>>1) + (font->descent>>1),
			      paint->x + paint->width,
			      paint->y - (font->ascent>>1) + (font->descent>>1));
		}
	    }
	}

	paint = paint->next;
    }

    /* pass 6: bearing clipping */
    /* dont care on order of drawing or caching of state (by now) */
    paint = list->bearings;
    while (paint) {
	XRectangle rect2;

	if (paint->highlight)
	    XSetForeground(XtDisplay(ctx), gc, sink->text_sink.background);
	if (!paint->property || !(paint->property->mask & XAW_TPROP_FONT))
	    font = sink->ascii_sink.font;
	else
	    font = paint->property->font;
	XSetFont(XtDisplay(ctx), gc, font->fid);
	if (!paint->highlight) {
	    if (!paint->property ||
		!(paint->property->mask & XAW_TPROP_FOREGROUND))
		XSetForeground(XtDisplay(ctx), gc, sink->text_sink.foreground);
	    else
		XSetForeground(XtDisplay(ctx), gc, paint->property->foreground);
	}
	if (paint->x < XtWidth(ctx) && paint->x + paint->width > 0) {
	    rect2.x = (short)(paint->x + paint->width);
	    rect2.width = (XawAbs(paint->width));	    /* more than enough */
	    rect2.y = (short)(paint->y - font->ascent);
	    rect2.height = (unsigned short)(rect2.y + font->ascent + font->descent);
	    XSetClipRectangles(XtDisplay((Widget)ctx), gc,
			       0, 0, &rect2, 1, Unsorted);
	    XDrawString(XtDisplay(ctx), XtWindow(ctx), gc, paint->x, paint->y,
			paint->text, (int)paint->length);
	}
	paint = paint->next;
    }
}
#endif

/*
 * Function:
 *	PaintText
 *
 * Parameters:
 *	w   - text sink object
 *	gc  - gc to paint text with
 *	x   - location to paint the text
 *	y   - ""
 *	buf - buffer and length of text to paint.
 *	len - ""
 *	clear_bg - clear background before drawing ?
 *
 * Description:
 *	Actually paints the text into the window.
 *
 * Returns:
 *	the width of the text painted
 */
static unsigned int
PaintText(Widget w, GC gc, int x, int y, char *buf, int len, Bool clear_bg)
{
    AsciiSinkObject sink = (AsciiSinkObject)w;
    TextWidget ctx = (TextWidget)XtParent(w);
    int width = XTextWidth(sink->ascii_sink.font, buf, len);

    if ((x > XtWidth(ctx)) || width <= -x) /* Don't draw if we can't see it */
	return (unsigned)(width);

    if (clear_bg) {
	_XawTextSinkClearToBackground(w, x, y - sink->ascii_sink.font->ascent,
				     (unsigned)width,
				     (unsigned)(sink->ascii_sink.font->ascent
				     + sink->ascii_sink.font->descent));
	XDrawString(XtDisplay(ctx), XtWindow(ctx), gc, x, y, buf, len);
    }
    else
	XDrawImageString(XtDisplay(ctx), XtWindow(ctx), gc, x, y, buf, len);

    return (unsigned)(width);
}

static void
DisplayText(Widget w, int x, int y,
	    XawTextPosition pos1, XawTextPosition pos2, Bool highlight)
{
    TextWidget ctx = (TextWidget)XtParent(w);
    AsciiSinkObject sink = (AsciiSinkObject)w;
    XFontStruct *font = sink->ascii_sink.font;
    Widget source = XawTextGetSource(XtParent(w));
    unsigned char buf[260];
    int j, k;
    XawTextBlock blk;
    GC gc, invgc, tabgc;
    int max_x;
    Bool clear_bg;

    if (!sink->ascii_sink.echo || !ctx->text.lt.lines)
	return;

    max_x = (int)XtWidth(ctx) - ctx->text.r_margin.right;
    clear_bg = !highlight && ctx->core.background_pixmap != XtUnspecifiedPixmap;

    gc = highlight ? sink->ascii_sink.invgc : sink->ascii_sink.normgc;
    invgc = highlight ? sink->ascii_sink.normgc : sink->ascii_sink.invgc;

    if (highlight && sink->ascii_sink.xorgc)
	tabgc = sink->ascii_sink.xorgc;
    else
	tabgc = invgc;

    y += sink->ascii_sink.font->ascent;
    for (j = 0; pos1 < pos2;) {
	pos1 = XawTextSourceRead(source, pos1, &blk, (int)(pos2 - pos1));
	for (k = 0; k < blk.length; k++) {
	    if ((unsigned)j >= sizeof(buf) - 4) {	/* buffer full, dump the text */
		if ((x = (int)((unsigned)x + PaintText(w, gc, x, y, (char*)buf, j, clear_bg)))
		    >= max_x)
		    return;
		j = 0;
	    }
	    buf[j] = (unsigned char)blk.ptr[k];
	    if (buf[j] == XawLF)	/* line feeds ('\n') are not printed */
		continue;

	    else if (buf[j] == '\t') {
		int width;

		if (j != 0
		    && (x = (int)((unsigned)x + PaintText(w, gc, x, y, (char*)buf, j, clear_bg)))
		    >= max_x)
		    return;

		if ((width = CharWidth(sink, font, x, '\t')) > -x) {
		    if (clear_bg)
			_XawTextSinkClearToBackground(w, x, y-font->ascent, (unsigned)width,
						      (unsigned)(font->ascent+font->descent));
		    else
			XFillRectangle(XtDisplayOfObject(w), XtWindowOfObject(w),
				       tabgc, x, y - font->ascent, (unsigned)width,
				       (unsigned)(font->ascent + font->descent));
		}

		if ((x += width) >= max_x)
		    return;

		j = -1;
	    }
	    else if ((buf[j] & 0177) < XawSP || buf[j] == 0177) {
		if (sink->ascii_sink.display_nonprinting) {
		    unsigned char c = buf[j];

		    if (c > 0177) {
			buf[j++] = '\\';
			buf[j++] = (unsigned char)(((c >> 6) & 7) + '0');
			buf[j++] = (unsigned char)(((c >> 3) & 7) + '0');
			buf[j] = (unsigned char)((c & 7) + '0');
		    }
		    else {
			c |= 0100;
			buf[j++] = '^';
			buf[j] = c == 0177 ? '?' : c;
		    }
		}
		else
		  buf[j] = ' ';
	    }
	    j++;
	}
    }

    if (j > 0)
	(void)PaintText(w, gc, x, y, (char*)buf, j, clear_bg);
}

/*
 * Function:
 *	GetCursorBounds
 *
 * Parameters:
 *	w - text sink object
 *	rect - X rectangle to return the cursor bounds
 *
 * Description:
 *	Returns the size and location of the cursor.
 */
static void
GetCursorBounds(Widget w, XRectangle *rect)
{
    AsciiSinkObject sink = (AsciiSinkObject)w;
    XFontStruct *font = sink->ascii_sink.font;
    unsigned char ch;
#ifndef OLDXAW
    TextWidget ctx = (TextWidget)XtParent(w);
    XawTextBlock block;
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextProperty *property;

    if (XawTextSourceAnchorAndEntity(XawTextGetSource(XtParent(w)),
				     sink->ascii_sink.cursor_position,
				     &anchor, &entity)) {
	if ((property = XawTextSinkGetProperty((Widget)sink,
					       entity->property)) != NULL &&
	    (property->mask & XAW_TPROP_FONT))
	    font = property->font;
    }
    (void)XawTextSourceRead(XawTextGetSource((Widget)ctx),
			    ctx->text.insertPos, &block, 1);
    if (!block.length || block.ptr[0] == '\n' || block.ptr[0] == '\t')
	ch = ' ';
    else if ((*((unsigned char*)block.ptr) & 0177) < XawSP ||
	*(unsigned char*)block.ptr == 0177) {
	if (sink->ascii_sink.display_nonprinting)
	    ch = *((unsigned char*)block.ptr) > 0177 ? '\\' : '^';
	else
	    ch = ' ';
    }
    else
	ch = *(unsigned char*)block.ptr;
#else
    ch = ' ';
#endif

    rect->width = (unsigned short)(CharWidth(sink, font, 0, ch));
    rect->height = (unsigned short)(font->descent + font->ascent + 1);

    rect->x = sink->ascii_sink.cursor_x;
    rect->y = (short)(sink->ascii_sink.cursor_y - font->ascent);
}

/* this function is required to support different fonts and correctly place
 * the cursor. There are better ways to calculate the base line, but there is
 * no place/code (yet) to store this information.
 */
static int
FindCursorY(TextWidget ctx, XawTextPosition position)
{
    int y, line, ascent;
    AsciiSinkObject sink = (AsciiSinkObject)ctx->text.sink;
#ifndef OLDXAW
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextProperty *property;
    XawTextPosition left, right;
#endif

    for (line = 0; line < ctx->text.lt.lines; line++)
	if (position < ctx->text.lt.info[line + 1].position)
	    break;

    y = ctx->text.lt.info[line].y;
#ifndef OLDXAW
    ascent = 0;
    left = ctx->text.lt.info[line].position;
    right = ctx->text.lt.info[line + 1].position;
    right = XawMin(right, ctx->text.lastPos + 1);
    while (left < right) {
	if (XawTextSourceAnchorAndEntity(ctx->text.source, left,
					 &anchor, &entity)) {
	    if ((property = XawTextSinkGetProperty((Widget)sink,
						   entity->property)) != NULL &&
		(property->mask & XAW_TPROP_FONT))
		ascent = XawMax(property->font->ascent, ascent);
	    else
		ascent = XawMax(sink->ascii_sink.font->ascent, ascent);
	    left = anchor->position + entity->offset + entity->length;
	}
	else if (anchor) {
	    ascent = XawMax(sink->ascii_sink.font->ascent, ascent);
	    while (entity) {
		XawTextPosition tmp = anchor->position + entity->offset + entity->length;

		if (tmp > left && tmp < right) {
		    left = tmp;
		    if ((property = XawTextSinkGetProperty((Widget)sink,
							   entity->property)) != NULL &&
			(property->mask & XAW_TPROP_FONT))
			ascent = XawMax(property->font->ascent, ascent);
		    else
			ascent = XawMax(sink->ascii_sink.font->ascent, ascent);
		}
		entity = entity->next;
	    }
	    if (entity == NULL)
		break;
	}
	else {
	    ascent = XawMax(sink->ascii_sink.font->ascent, ascent);
	    break;
	}
    }
    if (!ascent)
	ascent = sink->ascii_sink.font->ascent;
#else
    ascent = sink->ascii_sink.font->ascent;
#endif

    return (y + ascent);
}

static void
InsertCursor(Widget w, int x, int y, XawTextInsertState state)
{
    AsciiSinkObject sink = (AsciiSinkObject)w;
    XFontStruct *font = sink->ascii_sink.font;
    TextWidget ctx = (TextWidget)XtParent(w);
    XawTextPosition position = XawTextGetInsertionPoint((Widget)ctx);
    Boolean overflow = ((unsigned)x & 0xffff8000) != 0;
#ifndef OLDXAW
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextProperty *property;
#endif

    if (XtIsRealized((Widget)ctx)) {
	int fheight;
	XawTextBlock block;
	XawTextPosition selection_start, selection_end;
	Boolean has_selection;

	if (!sink->ascii_sink.echo) {
	    if (sink->ascii_sink.laststate != state) {
		int width = CharWidth(sink, font, 0, ' ') - 1;

		x = ctx->text.margin.left;
		y = ctx->text.margin.top;
		font = sink->ascii_sink.font;
		fheight = font->ascent + font->descent;
		if (state == XawisOn) {
		    if (ctx->text.hasfocus)
		    XFillRectangle(XtDisplay(ctx), XtWindow(ctx),
				   sink->ascii_sink.xorgc, x, y,
				   (unsigned)(width + 1), (unsigned)(fheight + 1));
		    else
			XDrawRectangle(XtDisplay(ctx), XtWindow(ctx),
				       sink->ascii_sink.xorgc, x, y,
				       (unsigned)width, (unsigned)fheight);

		}
		else
		    _XawTextSinkClearToBackground(w, x, y,
						  (unsigned)(width + 1), (unsigned)(fheight + 1));
	    }
	    sink->ascii_sink.cursor_x = (short)x;
	    sink->ascii_sink.cursor_y = (short)y;
	    sink->ascii_sink.laststate = state;
	    return;
	}


	XawTextGetSelectionPos((Widget)ctx, &selection_start, &selection_end);
	has_selection = selection_start != selection_end;

	if (sink->ascii_sink.laststate != state) {
	    unsigned char ch;

#ifndef OLDXAW
	    if (XawTextSourceAnchorAndEntity(ctx->text.source,
					     position, &anchor, &entity) &&
		(property = XawTextSinkGetProperty((Widget)sink,
						   entity->property)) != NULL &&
		(property->mask & XAW_TPROP_FONT))
		font = property->font;
	    else
		font = sink->ascii_sink.font;
#endif

	    fheight = font->ascent + font->descent;
	    (void)XawTextSourceRead(XawTextGetSource((Widget)ctx),
				    position, &block, 1);
	    if (!block.length || block.ptr[0] == '\n' || block.ptr[0] == '\t')
		ch = ' ';
	    else if ((*((unsigned char*)block.ptr) & 0177) < XawSP
		|| *(unsigned char*)block.ptr == 0177) {
		if (sink->ascii_sink.display_nonprinting)
		    ch = *((unsigned char*)block.ptr) > 0177 ? '\\' : '^';
		else
		    ch = ' ';
	    }
	    else
		ch = *(unsigned char*)block.ptr;

	    y = FindCursorY(ctx, position);
	    if (ctx->text.hasfocus && !has_selection)
		XFillRectangle(XtDisplay(ctx), XtWindow(ctx),
			       sink->ascii_sink.xorgc, x, y - font->ascent,
			       (unsigned)CharWidth(sink, font, 0, ch),
			       (unsigned)(fheight + 1));
	    else
		XDrawRectangle(XtDisplay(ctx), XtWindow(ctx),
			       sink->ascii_sink.xorgc, x, y - font->ascent,
			       (unsigned)(CharWidth(sink, font, 0, ch) - 1),
			       (unsigned)fheight);
	}
    }

    sink->ascii_sink.cursor_x = (short)(overflow ? -16384 : x);
    sink->ascii_sink.cursor_y = (short)y;
    sink->ascii_sink.laststate = state;
    sink->ascii_sink.cursor_position = position;
}

/*
 * Given two positions, find the distance between them
 */
static void
FindDistance(Widget w, XawTextPosition fromPos, int fromx,
	     XawTextPosition toPos, int *resWidth,
	     XawTextPosition *resPos, int *resHeight)
{
#ifndef OLDXAW
    AsciiSinkObject sink = (AsciiSinkObject)w;
    TextWidget ctx = (TextWidget)XtParent(w);
    XFontStruct *font = sink->ascii_sink.font;
    Widget source = ctx->text.source;
    XawTextPosition idx, pos;
    unsigned char c;
    XawTextBlock blk;
    int i, rWidth, ascent = 0, descent = 0;
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextProperty *property;
    Cardinal length;
    Bool done = False;

    pos = idx = fromPos;
    rWidth = 0;
    c = 0;

    while (!done) {
	if (XawTextSourceAnchorAndEntity(source, pos, &anchor, &entity)) {
	    length = (Cardinal)(anchor->position + entity->offset + entity->length);
	    length = (XawMin(toPos, length) - pos);
	    if ((property = XawTextSinkGetProperty((Widget)sink,
						   entity->property)) != NULL &&
		(property->mask & XAW_TPROP_FONT))
		font = property->font;
	    else
		font = sink->ascii_sink.font;
	}
	else {
	    if (anchor) {
		while (entity && anchor->position + entity->offset < pos)
		    entity = entity->next;
		if (entity) {
		    length = (Cardinal)(anchor->position + entity->offset);
		    length = (XawMin(toPos, length) - pos);
		}
		else
		    length = (XawMin(toPos - pos, 4096));
	    }
	    else
		length = (XawMin(toPos - pos, 4096));
	    font = sink->ascii_sink.font;
	}

	ascent = XawMax(font->ascent, ascent);
	descent = XawMax(font->descent, descent);

	pos = XawTextSourceRead(source, pos, &blk, (int)length);
	if (blk.length == 0 && pos == idx)	/* eof reached */
	    break;

	idx = blk.firstPos;
	for (i = 0; idx < toPos; i++, idx++) {
	    if (i >= blk.length)
		break;
	    c = (unsigned char)blk.ptr[i];
	    rWidth += CharWidth(sink, font, fromx + rWidth, c);
	    if (c == XawLF) {
		idx++;
		done = True;
		break;
	    }
	}
	if (idx >= toPos)
	    break;
    }

    *resPos = idx;
    *resWidth = rWidth;
    *resHeight = ascent + descent + 1;
#else
    AsciiSinkObject sink = (AsciiSinkObject)w;
    TextWidget ctx = (TextWidget)XtParent(w);
    XFontStruct *font = sink->ascii_sink.font;
    Widget source = ctx->text.source;
    XawTextPosition idx, pos;
    unsigned char c;
    XawTextBlock blk;
    int i, rWidth;

    pos = XawTextSourceRead(source, fromPos, &blk, toPos - fromPos);
    rWidth = 0;
    for (i = 0, idx = fromPos; idx < toPos; i++, idx++) {
	if (i >= blk.length) {
	    i = 0;
	    pos = XawTextSourceRead(source, pos, &blk, toPos - pos);
	    if (blk.length == 0)
		break;
	}
	c = (unsigned char)blk.ptr[i];
	rWidth += CharWidth(sink, font, fromx + rWidth, c);
	if (c == XawLF) {
	    idx++;
	    break;
	}
    }

    *resPos = idx;
    *resWidth = rWidth;
    *resHeight = font->ascent + font->descent + 1;
#endif
}

static void
FindPosition(Widget w, XawTextPosition fromPos, int fromx, int width,
	     Bool stopAtWordBreak, XawTextPosition *resPos,
	     int *resWidth, int *resHeight)
{
#ifndef OLDXAW
    AsciiSinkObject sink = (AsciiSinkObject)w;
    TextWidget ctx = (TextWidget)XtParent(w);
    Widget source = ctx->text.source;
    XFontStruct *font = sink->ascii_sink.font;
    XawTextPosition idx, pos, whiteSpacePosition = 0;
    int i, lastWidth, whiteSpaceWidth, rWidth, ascent = 0, descent = 0;
    Boolean whiteSpaceSeen;
    unsigned char c;
    XawTextBlock blk;
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextProperty *property;
    Cardinal length;
    Bool done = False;

    pos = idx = fromPos;
    rWidth = lastWidth = whiteSpaceWidth = 0;
    whiteSpaceSeen = False;
    c = 0;

    while (!done) {
	font = sink->ascii_sink.font;
	if (XawTextSourceAnchorAndEntity(source, pos, &anchor, &entity)) {
	    length = (Cardinal)(anchor->position + entity->offset + entity->length - pos);
	    if ((property = XawTextSinkGetProperty((Widget)sink,
						   entity->property)) != NULL &&
		(property->mask & XAW_TPROP_FONT))
		font = property->font;
	}
	else {
	    if (anchor) {
		while (entity && anchor->position + entity->offset < pos)
		    entity = entity->next;
		if (entity)
		    length = (Cardinal)(anchor->position + entity->offset - pos);
		else
		    length = 4096;
	    }
	    else
		length = 4096;
	}

	ascent = XawMax(font->ascent, ascent);
	descent = XawMax(font->descent, descent);

	pos = XawTextSourceRead(source, pos, &blk, (int)length);
	if (blk.length == 0 && pos == idx)	/* eof reached */
	    break;

	idx = blk.firstPos;
	for (i = 0; rWidth <= width && i < blk.length; i++, idx++) {
	    c = (unsigned char)blk.ptr[i];
	    lastWidth = rWidth;
	    rWidth += CharWidth(sink, font, fromx + rWidth, c);

	    if (c == XawLF) {
		idx++;
		done = True;
		break;
	    }
	    else if ((c == XawSP || c == XawTAB) && rWidth <= width) {
		whiteSpaceSeen = True;
		whiteSpacePosition = idx;
		whiteSpaceWidth = rWidth;
	    }
	}
	if (rWidth > width)
	    break;
    }

    if (rWidth > width && idx > fromPos) {
	idx--;
	rWidth = lastWidth;
	if (stopAtWordBreak && whiteSpaceSeen) {
	    idx = whiteSpacePosition + 1;
	    rWidth = whiteSpaceWidth;
	}
    }

    if (idx >= ctx->text.lastPos && c != XawLF)
	idx = ctx->text.lastPos + 1;

    *resPos = idx;
    *resWidth = rWidth;
    *resHeight = ascent + descent + 1;
#else
    AsciiSinkObject sink = (AsciiSinkObject)w;
    TextWidget ctx = (TextWidget)XtParent(w);
    Widget source = ctx->text.source;
    XFontStruct *font = sink->ascii_sink.font;
    XawTextPosition idx, pos, whiteSpacePosition = 0;
    int i, lastWidth, whiteSpaceWidth, rWidth;
    Boolean whiteSpaceSeen;
    unsigned char c;
    XawTextBlock blk;

    pos = XawTextSourceRead(source, fromPos, &blk, BUFSIZ);
    rWidth = lastWidth = whiteSpaceWidth = 0;
    whiteSpaceSeen = False;
    c = 0;

    for (i = 0, idx = fromPos; rWidth <= width; i++, idx++) {
	if (i >= blk.length) {
	    i = 0;
	    pos = XawTextSourceRead(source, pos, &blk, BUFSIZ);
	    if (blk.length == 0)
		break;
	}
	c = (unsigned char)blk.ptr[i];
	lastWidth = rWidth;
	rWidth += CharWidth(sink, font, fromx + rWidth, c);

	if (c == XawLF) {
	    idx++;
	    break;
	}
	else if ((c == XawSP || c == XawTAB) && rWidth <= width) {
	    whiteSpaceSeen = True;
	    whiteSpacePosition = idx;
	    whiteSpaceWidth = rWidth;
	}
    }

    if (rWidth > width && idx > fromPos) {
	idx--;
	rWidth = lastWidth;
	if (stopAtWordBreak && whiteSpaceSeen) {
	    idx = whiteSpacePosition + 1;
	    rWidth = whiteSpaceWidth;
	}
    }

    if (idx >= ctx->text.lastPos && c != XawLF)
	idx = ctx->text.lastPos + 1;

    *resPos = idx;
    *resWidth = rWidth;
    *resHeight = font->ascent + font->descent + 1;
#endif
}

static void
Resolve(Widget w, XawTextPosition pos, int fromx, int width,
	XawTextPosition *pos_return)
{
    int resWidth, resHeight;
    Widget source = XawTextGetSource(XtParent(w));

    FindPosition(w, pos, fromx, width, False, pos_return, &resWidth, &resHeight);
    if (*pos_return > GETLASTPOS)
	*pos_return = GETLASTPOS;
}

static void
GetGC(AsciiSinkObject sink)
{
    XtGCMask valuemask = (GCFont | GCGraphicsExposures | GCClipXOrigin |
			  GCForeground | GCBackground);
    XGCValues values;

    /* XXX We dont want do share a gc that will change the clip-mask */
    values.clip_x_origin = (int)(long)sink;
    values.clip_mask = None;
    values.font = sink->ascii_sink.font->fid;
    values.graphics_exposures = False;

    values.foreground = sink->text_sink.foreground;
    values.background = sink->text_sink.background;
    sink->ascii_sink.normgc = XtAllocateGC((Widget)sink, 0, valuemask, &values,
					   GCClipMask | GCFont | GCForeground |
					   GCBackground, 0);

    values.foreground = sink->text_sink.background;
#ifndef OLDXAW
    values.background = sink->text_sink.cursor_color;
#else
    values.background = sink->text_sink.foreground;
#endif
    sink->ascii_sink.invgc = XtAllocateGC((Widget)sink, 0, valuemask, &values,
					  GCClipMask | GCFont, 0);

    valuemask |= GCFunction;
    values.function = GXxor;
#ifndef OLDXAW
    values.foreground = sink->text_sink.background ^ sink->text_sink.cursor_color;
#else
    values.foreground = sink->text_sink.background ^ sink->text_sink.foreground;
#endif
    values.background = 0L;
    sink->ascii_sink.xorgc = XtAllocateGC((Widget)sink, 0, valuemask,
					  &values, GCClipMask | GCFont, 0);

    XawAsciiSinkResize((Widget)sink);
}

/* Function:
 *	XawAsciiSinkInitialize
 *
 * Parameters:
 *	request - the requested and new values for the object instance
 *	cnew	- ""
 *
 * Description:
 *	Initializes the TextSink Object.
 */
/*ARGSUSED*/
static void
XawAsciiSinkInitialize(Widget request _X_UNUSED, Widget cnew,
		       ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    AsciiSinkObject sink = (AsciiSinkObject)cnew;

    if (!sink->ascii_sink.font) XtError("Aborting: no font found\n");

    GetGC(sink);

    sink->ascii_sink.cursor_position = 0;
    sink->ascii_sink.laststate = XawisOff;
    sink->ascii_sink.cursor_x = sink->ascii_sink.cursor_y = 0;
}

/*
 * Function:
 *	XawAsciiSinkDestroy
 *
 * Parameters:
 *	w - AsciiSink Object
 *
 * Description:
 *	This function cleans up when the object is destroyed.
 */
static void
XawAsciiSinkDestroy(Widget w)
{
    AsciiSinkObject sink = (AsciiSinkObject)w;

    XtReleaseGC(w, sink->ascii_sink.normgc);
    XtReleaseGC(w, sink->ascii_sink.invgc);
    XtReleaseGC(w, sink->ascii_sink.xorgc);

    sink->ascii_sink.normgc =
      sink->ascii_sink.invgc =
      sink->ascii_sink.xorgc = NULL;
}

static void
XawAsciiSinkResize(Widget w)
{
    TextWidget ctx = (TextWidget)XtParent(w);
    AsciiSinkObject sink = (AsciiSinkObject)w;
    XRectangle rect;
    int width, height;

    if (w->core.widget_class != asciiSinkObjectClass)
	return;

    rect.x = ctx->text.r_margin.left;
    rect.y = ctx->text.r_margin.top;
    width = (int)XtWidth(ctx) - RHMargins(ctx);
    height = (int)XtHeight(ctx) - RVMargins(ctx);
    rect.width = (unsigned short)width;
    rect.height = (unsigned short)height;

    if (sink->ascii_sink.normgc) {
	if (width >= 0 && height >= 0)
	    XSetClipRectangles(XtDisplay((Widget)ctx), sink->ascii_sink.normgc,
			       0, 0, &rect, 1, Unsorted);
	else
	    XSetClipMask(XtDisplay((Widget)ctx), sink->ascii_sink.normgc, None);
    }
    if (sink->ascii_sink.invgc) {
	if (width >= 0 && height >= 0)
	    XSetClipRectangles(XtDisplay((Widget)ctx), sink->ascii_sink.invgc,
			       0, 0, &rect, 1, Unsorted);
	else
	    XSetClipMask(XtDisplay((Widget)ctx), sink->ascii_sink.invgc, None);
    }
    if (sink->ascii_sink.xorgc) {
	if (width >= 0 && height >= 0)
	    XSetClipRectangles(XtDisplay((Widget)ctx), sink->ascii_sink.xorgc,
			       0, 0, &rect, 1, Unsorted);
	else
	    XSetClipMask(XtDisplay((Widget)ctx), sink->ascii_sink.xorgc, None);
    }
}

/*
 * Function:
 *	XawAsciiSinkSetValues
 *
 * Parameters:
 *	current - current state of the object
 *	request - what was requested
 *	cnew    - what the object will become
 *
 * Description:
 *	Sets the values for the AsciiSink.
 *
 * Returns:
 *	True if redisplay is needed
 */
/*ARGSUSED*/
static Boolean
XawAsciiSinkSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		      ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    AsciiSinkObject w = (AsciiSinkObject)cnew;
    AsciiSinkObject old_w = (AsciiSinkObject)current;

    if (w->ascii_sink.font != old_w->ascii_sink.font
	|| w->text_sink.background != old_w->text_sink.background
	|| w->text_sink.foreground != old_w->text_sink.foreground
#ifndef OLDXAW
	|| w->text_sink.cursor_color != old_w->text_sink.cursor_color
	|| w->text_sink.properties != old_w->text_sink.properties
#endif
	) {
#ifdef OLDXAW
	XtReleaseGC(cnew, w->ascii_sink.normgc);
	XtReleaseGC(cnew, w->ascii_sink.invgc);
	XtReleaseGC(cnew, w->ascii_sink.xorgc);
	GetGC(w);
#endif
	((TextWidget)XtParent(cnew))->text.redisplay_needed = True;
    }
    else if (w->ascii_sink.echo != old_w->ascii_sink.echo
	     || w->ascii_sink.display_nonprinting
	     != old_w->ascii_sink.display_nonprinting)
      ((TextWidget)XtParent(cnew))->text.redisplay_needed = True;
#ifndef OLDXAW
    if (w->text_sink.properties != old_w->text_sink.properties) {
	XawTextProperty *property =
	    XawTextSinkGetProperty(cnew, XrmStringToQuark("default"));

	if (property) {
	    if (property->mask & XAW_TPROP_FONT)
		w->ascii_sink.font = property->font;
	    if (property->mask & XAW_TPROP_FOREGROUND)
		w->text_sink.foreground = property->foreground;
	    if (property->mask & XAW_TPROP_BACKGROUND)
		w->text_sink.background = property->background;
	}
    }
#endif

    return (False);
}

/*
 * Function:
 *	MaxLines
 *
 * Parameters:
 *	w      - AsciiSink Object
 *	height - height to fit lines into
 *
 * Description:
 *	Finds the Maximum number of lines that will fit in a given height.
 *
 * Returns:
 *	The number of lines that will fit
 */
/*ARGSUSED*/
static int
MaxLines(Widget w, unsigned int height)
{
    AsciiSinkObject sink = (AsciiSinkObject)w;
    int font_height;

    font_height = sink->ascii_sink.font->ascent + sink->ascii_sink.font->descent + 1;

    return ((int)height / font_height);
}

/*
 * Function:
 *	MaxHeight
 *
 * Parameters:
 *	w     - AsciiSink Object
 *	lines - number of lines
 *
 * Description:
 *	Finds the Minimum height that will contain a given number lines.
 *
 * Returns:
 *	the height
 */
static int
MaxHeight(Widget w, int lines)
{
    AsciiSinkObject sink = (AsciiSinkObject)w;

    return (lines * (sink->ascii_sink.font->ascent +
		     sink->ascii_sink.font->descent + 1));
}

/*
 * Function:
 *	SetTabs
 *
 * Parameters:
 *	w	  - AsciiSink Object
 *	tab_count - number of tabs in the list
 *	tabs	  - text positions of the tabs
 *
 * Description:
 *	Sets the Tab stops.
 */
static void
SetTabs(Widget w, int tab_count, short *tabs)
{
    AsciiSinkObject sink = (AsciiSinkObject)w;
    int i;
    Atom XA_FIGURE_WIDTH;
    unsigned long figure_width = 0;
    XFontStruct *font = sink->ascii_sink.font;

    /*
     * Find the figure width of the current font
     */
    XA_FIGURE_WIDTH = XInternAtom(XtDisplayOfObject(w), "FIGURE_WIDTH", False);
    if (XA_FIGURE_WIDTH != None
	&& (!XGetFontProperty(font, XA_FIGURE_WIDTH, &figure_width)
	    || figure_width == 0)) {
	if (font->per_char && font->min_char_or_byte2 <= '$'
	    && font->max_char_or_byte2 >= '$')
	    figure_width = (unsigned long)font->per_char['$' - font->min_char_or_byte2].width;
	else
	    figure_width = (unsigned long)font->max_bounds.width;
      }

    if (tab_count > sink->text_sink.tab_count) {
	sink->text_sink.tabs = (Position *)
	  XtRealloc((char*)sink->text_sink.tabs, (Cardinal)((size_t)tab_count * sizeof(Position)));
	sink->text_sink.char_tabs = (short *)
	  XtRealloc((char*)sink->text_sink.char_tabs, (Cardinal)((size_t)tab_count * sizeof(short)));
    }

    for (i = 0 ; i < tab_count ; i++) {
	sink->text_sink.tabs[i] = (Position)((size_t)tabs[i] * figure_width);
	sink->text_sink.char_tabs[i] = tabs[i];
      }

    sink->text_sink.tab_count = tab_count;

#ifndef NO_TAB_FIX
    {
	TextWidget ctx = (TextWidget)XtParent(w);
	ctx->text.redisplay_needed = True;
	_XawTextBuildLineTable(ctx, ctx->text.lt.top, True);
    }
#endif
}
