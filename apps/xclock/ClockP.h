/*
* $Xorg: ClockP.h,v 1.4 2001/02/09 02:05:39 xorgcvs Exp $
*/


/***********************************************************

Copyright 1987, 1988, 1998  The Open Group

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
/* $XFree86: xc/programs/xclock/ClockP.h,v 1.11 2002/10/17 01:00:01 dawes Exp $ */

#ifndef _XawClockP_h
#define _XawClockP_h

#include <X11/Xos.h>		/* Needed for struct tm. */
#include "Clock.h"
#include <X11/Xaw/SimpleP.h>
#ifdef XRENDER
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#endif

#define SEG_BUFF_SIZE		128
#ifdef NO_I18N
#define ASCII_TIME_BUFLEN	32	/* big enough for 26 plus slop */
#define STRFTIME_BUFF_SIZE      100     /* buffer for "strftime" option */
#else
#define STRFTIME_BUFF_SIZE	256	/* should handle any locale */
#endif



/* New fields for the clock widget instance record */
typedef struct {
#ifndef RENDER
	 Pixel	fgpixel;	/* color index for text */
#endif
	 Pixel	Hipixel;	/* color index for Highlighting */
	 Pixel	Hdpixel;	/* color index for hands */
	 XFontStruct	*font;	/* font for text */
	 GC	myGC;		/* pointer to GraphicsContext */
	 GC	EraseGC;	/* eraser GC */
	 GC	HandGC;		/* Hand GC */
	 GC	HighGC;		/* Highlighting GC */
/* start of graph stuff */
	 float	update;		/* update period in second */
	 Dimension radius;		/* radius factor */
	 int	backing_store;	/* backing store type */
	 Boolean chime;
	 Boolean beeped;
	 Boolean analog;
	 Boolean brief;
	 Boolean twentyfour;
	 Boolean utime;
         String strftime;
	 Boolean show_second_hand;
	 Dimension second_hand_length;
	 Dimension minute_hand_length;
	 Dimension hour_hand_length;
	 Dimension hand_width;
	 Dimension second_hand_width;
	 Position centerX;
	 Position centerY;
	 int	numseg;
	 int	padding;
	 XPoint	segbuff[SEG_BUFF_SIZE];
	 XPoint	*segbuffptr;
	 XPoint	*hour, *sec;
	 struct tm  otm ;
	 struct timeval otv ;
	 XtIntervalId interval_id;
	 char prev_time_string[STRFTIME_BUFF_SIZE];
#ifndef NO_I18N
    	 XFontSet fontSet;     /* font set for localized text */
	 Boolean	utf8;
#endif
#ifdef XRENDER
	 XftColor	fg_color;
	 XftColor	hour_color;
	 XftColor	min_color;
	 XftColor	sec_color;
	 XftColor	major_color;
	 XftColor	minor_color;
	 XftFont	*face;
	 XRenderPictFormat  *mask_format;

	 Boolean    render;
	 Boolean    sharp;
	 Boolean    can_polygon;
	 Boolean    buffer;
	 XftDraw    *draw;
	 Picture    picture;
	 Picture    fill_picture;
	 Pixmap	    pixmap;
	 XRectangle damage;
	 XDouble    x_scale;
	 XDouble    x_off;
	 XDouble    y_scale;
	 XDouble    y_off;
#endif
   } ClockPart;

#ifdef XRENDER
#define ClockFgPixel(c)	((c)->clock.fg_color.pixel)
#else
#define ClockFgPixel(c)	((c)->clock.fgpixel)
#endif

/* Full instance record declaration */
typedef struct _ClockRec {
   CorePart core;
   SimplePart simple;
   ClockPart clock;
   } ClockRec;

/* New fields for the Clock widget class record */
typedef struct {int dummy;} ClockClassPart;

/* Full class record declaration. */
typedef struct _ClockClassRec {
   CoreClassPart core_class;
   SimpleClassPart simple_class;
   ClockClassPart clock_class;
   } ClockClassRec;

/* Class pointer. */
extern ClockClassRec clockClassRec;

#endif /* _XawClockP_h */
