/* $Xorg: Clock.c,v 1.4 2001/02/09 02:05:39 xorgcvs Exp $ */
/* $XdotOrg: xc/programs/xclock/Clock.c,v 1.3 2004/10/30 20:33:44 alanc Exp $ */

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
/*
 * Copyright (c) 2004, Oracle and/or its affiliates. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Authors:  I18N - Steve Swales - March 2000
 *	     bgpixmap - Alan Coopersmith (as part of STSF project) - Sept. 2001
 */
/* $XFree86: xc/programs/xclock/Clock.c,v 3.25 2003/07/04 16:24:30 eich Exp $ */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <X11/Xlib.h>
#include <X11/StringDefs.h>
#include <X11/IntrinsicP.h>
#include "ClockP.h"
#include <X11/Xosdefs.h>
#include <stdio.h>
#include <X11/Xos.h>
#include <X11\Xwinsock.h>
#include <X11/Xaw/XawInit.h>
#if !defined(NO_I18N) && defined(HAVE_ICONV)
#include <iconv.h>
#include <langinfo.h>
#include <errno.h>
#include <limits.h>
#endif

#if defined(XawVersion) && (XawVersion >= 7000002L)
#define USE_XAW_PIXMAP_CVT
#else
#include <X11/xpm.h>
#endif

#include <time.h>
#define Time_t time_t

#ifdef XKB
#include <X11/extensions/XKBbells.h>
#endif

#ifndef NO_I18N
#include <stdlib.h> /* for getenv() */
#include <locale.h>
extern Boolean no_locale; /* if True, use old (unlocalized) behaviour */
#endif
#include <unistd.h>

/* Private Definitions */

#define VERTICES_IN_HANDS	6	/* to draw triangle */
#define PI			3.14159265358979
#define TWOPI			(2. * PI)

#define MINOR_TICK_FRACT	95
#define SECOND_HAND_FRACT	90
#define MINUTE_HAND_FRACT	70
#define HOUR_HAND_FRACT		40
#define HAND_WIDTH_FRACT	7
#define SECOND_WIDTH_FRACT	5
#define SECOND_HAND_TIME	30

#define ANALOG_SIZE_DEFAULT	164

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
/* #define abs(a) ((a) < 0 ? -(a) : (a)) */


/* Initialization of defaults */

#define offset(field) XtOffsetOf(ClockRec, clock.field)
#define goffset(field) XtOffsetOf(WidgetRec, core.field)

static XtResource resources[] = {
    {XtNwidth, XtCWidth, XtRDimension, sizeof(Dimension),
	goffset(width), XtRImmediate, (XtPointer) 0},
    {XtNheight, XtCHeight, XtRDimension, sizeof(Dimension),
	goffset(height), XtRImmediate, (XtPointer) 0},
    {XtNupdate, XtCInterval, XtRInt, sizeof(int),
        offset(update), XtRImmediate, (XtPointer) 60 },
#ifndef XRENDER
    {XtNforeground, XtCForeground, XtRPixel, sizeof(Pixel),
        offset(fgpixel), XtRString, XtDefaultForeground},
#endif
    {XtNhand, XtCForeground, XtRPixel, sizeof(Pixel),
        offset(Hdpixel), XtRString, XtDefaultForeground},
    {XtNhighlight, XtCForeground, XtRPixel, sizeof(Pixel),
        offset(Hipixel), XtRString, XtDefaultForeground},
    {XtNutime, XtCBoolean, XtRBoolean, sizeof(Boolean),
	offset(utime), XtRImmediate, (XtPointer) FALSE},
    {XtNanalog, XtCBoolean, XtRBoolean, sizeof(Boolean),
        offset(analog), XtRImmediate, (XtPointer) TRUE},
    {XtNtwentyfour, XtCBoolean, XtRBoolean, sizeof(Boolean),
        offset(twentyfour), XtRImmediate, (XtPointer) TRUE},
    {XtNbrief, XtCBoolean, XtRBoolean, sizeof(Boolean),
        offset(brief), XtRImmediate, (XtPointer) FALSE},
    {XtNstrftime, XtCString, XtRString, sizeof(String),
        offset(strftime), XtRString, ""},
    {XtNchime, XtCBoolean, XtRBoolean, sizeof(Boolean),
	offset(chime), XtRImmediate, (XtPointer) FALSE },
    {XtNpadding, XtCMargin, XtRInt, sizeof(int),
        offset(padding), XtRImmediate, (XtPointer) 8},
    {XtNfont, XtCFont, XtRFontStruct, sizeof(XFontStruct *),
        offset(font), XtRString, XtDefaultFont},
#ifndef NO_I18N
     {XtNfontSet, XtCFontSet, XtRFontSet, sizeof(XFontSet),
        offset(fontSet), XtRString, XtDefaultFontSet},
#endif
    {XtNbackingStore, XtCBackingStore, XtRBackingStore, sizeof (int),
    	offset (backing_store), XtRString, "default"},
#ifdef XRENDER
    {XtNrender, XtCBoolean, XtRBoolean, sizeof(Boolean),
	offset(render), XtRImmediate, (XtPointer) TRUE },
    {XtNbuffer, XtCBoolean, XtRBoolean, sizeof(Boolean),
	offset(buffer), XtRImmediate, (XtPointer) TRUE },
    {XtNsharp, XtCBoolean, XtRBoolean, sizeof(Boolean),
	offset(sharp), XtRImmediate, (XtPointer) FALSE },
    {XtNforeground, XtCForeground, XtRXftColor, sizeof(XftColor),
        offset(fg_color), XtRString, XtDefaultForeground},
    {XtNhourColor, XtCForeground, XtRXftColor, sizeof(XftColor),
	offset(hour_color), XtRString, XtDefaultForeground},
    {XtNminuteColor, XtCForeground, XtRXftColor, sizeof(XftColor),
	offset(min_color), XtRString, XtDefaultForeground},
    {XtNsecondColor, XtCForeground, XtRXftColor, sizeof(XftColor),
	offset(sec_color), XtRString, XtDefaultForeground},
    {XtNmajorColor, XtCForeground, XtRXftColor, sizeof(XftColor),
	offset(major_color), XtRString, XtDefaultForeground},
    {XtNminorColor, XtCForeground, XtRXftColor, sizeof(XftColor),
	offset(minor_color), XtRString, XtDefaultForeground},
    {XtNface, XtCFace, XtRXftFont, sizeof (XftFont *),
	offset (face), XtRString, ""},
#endif
};

#undef offset
#undef goffset

static void ClassInitialize ( void );
static void Initialize ( Widget request, Widget new, ArgList args,
			 Cardinal *num_args );
static void Realize ( Widget gw, XtValueMask *valueMask,
		      XSetWindowAttributes *attrs );
static void Destroy ( Widget gw );
static void Resize ( Widget gw );
static void Redisplay ( Widget gw, XEvent *event, Region region );
static void clock_tic ( XtPointer client_data, XtIntervalId *id );
static void erase_hands ( ClockWidget w, struct tm *tm );
static void ClockAngle ( int tick_units, double *sinp, double *cosp );
static void DrawLine ( ClockWidget w, Dimension blank_length,
		       Dimension length, int tick_units );
static void DrawHand ( ClockWidget w, Dimension length, Dimension width,
		       int tick_units );
static void DrawSecond ( ClockWidget w, Dimension length, Dimension width,
			 Dimension offset, int tick_units );
static void SetSeg ( ClockWidget w, int x1, int y1, int x2, int y2 );
static void DrawClockFace ( ClockWidget w );
static int clock_round ( double x );
static Boolean SetValues ( Widget gcurrent, Widget grequest, Widget gnew,
			   ArgList args, Cardinal *num_args );
#if !defined(NO_I18N) && defined(HAVE_ICONV)
static char *clock_to_utf8(const char *str, int in_len);
#endif

ClockClassRec clockClassRec = {
    { /* core fields */
    /* superclass		*/	(WidgetClass) &simpleClassRec,
    /* class_name		*/	"Clock",
    /* widget_size		*/	sizeof(ClockRec),
    /* class_initialize		*/	ClassInitialize,
    /* class_part_initialize	*/	NULL,
    /* class_inited		*/	FALSE,
    /* initialize		*/	Initialize,
    /* initialize_hook		*/	NULL,
    /* realize			*/	Realize,
    /* actions			*/	NULL,
    /* num_actions		*/	0,
    /* resources		*/	resources,
    /* resource_count		*/	XtNumber(resources),
    /* xrm_class		*/	NULLQUARK,
    /* compress_motion		*/	TRUE,
    /* compress_exposure	*/	XtExposeCompressMaximal,
    /* compress_enterleave	*/	TRUE,
    /* visible_interest		*/	FALSE,
    /* destroy			*/	Destroy,
    /* resize			*/	Resize,
    /* expose			*/	Redisplay,
    /* set_values		*/	SetValues,
    /* set_values_hook		*/	NULL,
    /* set_values_almost	*/	XtInheritSetValuesAlmost,
    /* get_values_hook		*/	NULL,
    /* accept_focus		*/	NULL,
    /* version			*/	XtVersion,
    /* callback_private		*/	NULL,
    /* tm_table			*/	NULL,
    /* query_geometry           */	XtInheritQueryGeometry,
    /* display_accelerator      */	XtInheritDisplayAccelerator,
    /* extension                */	NULL
    },
    { /* simple fields */
    /* change_sensitive         */      XtInheritChangeSensitive
    },
    { /* clock fields */
    /* ignore                   */      0
    }
};

WidgetClass clockWidgetClass = (WidgetClass) &clockClassRec;

/****************************************************************
 *
 * Private Procedures
 *
 ****************************************************************/
#ifndef USE_XAW_PIXMAP_CVT
static void CvtStringToPixmap(
    XrmValue*           args,
    Cardinal*           num_args,
    XrmValuePtr         fromVal,
    XrmValuePtr         toVal
    )
{
    static Pixmap	pmap;
    Pixmap shapemask;
    char *name = (char *)fromVal->addr;
    Screen *screen;
    Display *dpy;

    if (*num_args != 1)
     XtErrorMsg("wrongParameters","cvtStringToPixmap","XtToolkitError",
             "String to pixmap conversion needs screen argument",
              (String *)NULL, (Cardinal *)NULL);

    if (strcmp(name, "None") == 0) {
        pmap = None;
    } else {
	screen = *((Screen **) args[0].addr);
	dpy = DisplayOfScreen(screen);

	XpmReadFileToPixmap(dpy, RootWindowOfScreen(screen), name, &pmap,
	  &shapemask, NULL);
    }

    (*toVal).size = sizeof(Pixmap);
    (*toVal).addr = (XPointer) &pmap ;
}
#endif

#ifdef XRENDER
static XtConvertArgRec xftColorConvertArgs[] = {
    {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.screen),
     sizeof(Screen *)},
    {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.colormap),
     sizeof(Colormap)}
};

#define	donestr(type, value, tstr) \
	{							\
	    if (toVal->addr != NULL) {				\
		if (toVal->size < sizeof(type)) {		\
		    toVal->size = sizeof(type);			\
		    XtDisplayStringConversionWarning(dpy, 	\
			(char*) fromVal->addr, tstr);		\
		    return False;				\
		}						\
		*(type*)(toVal->addr) = (value);		\
	    }							\
	    else {						\
		static type static_val;				\
		static_val = (value);				\
		toVal->addr = (XPointer)&static_val;		\
	    }							\
	    toVal->size = sizeof(type);				\
	    return True;					\
	}

static void
XmuFreeXftColor (XtAppContext app, XrmValuePtr toVal, XtPointer closure,
		 XrmValuePtr args, Cardinal *num_args)
{
    Screen	*screen;
    Colormap	colormap;
    XftColor	*color;

    if (*num_args != 2)
    {
	XtAppErrorMsg (app,
		       "freeXftColor", "wrongParameters",
		       "XtToolkitError",
		       "Freeing an XftColor requires screen and colormap arguments",
		       (String *) NULL, (Cardinal *)NULL);
	return;
    }

    screen = *((Screen **) args[0].addr);
    colormap = *((Colormap *) args[1].addr);
    color = (XftColor *) toVal->addr;
    XftColorFree (DisplayOfScreen (screen),
		  DefaultVisual (DisplayOfScreen (screen),
				 XScreenNumberOfScreen (screen)),
		  colormap, color);
}

static Boolean
XmuCvtStringToXftColor(Display *dpy,
		       XrmValue *args, Cardinal *num_args,
		       XrmValue *fromVal, XrmValue *toVal,
		       XtPointer *converter_data)
{
    char	    *spec;
    XRenderColor    renderColor;
    XftColor	    xftColor;
    Screen	    *screen;
    Colormap	    colormap;

    if (*num_args != 2)
    {
	XtAppErrorMsg (XtDisplayToApplicationContext (dpy),
		       "cvtStringToXftColor", "wrongParameters",
		       "XtToolkitError",
		       "String to render color conversion needs screen and colormap arguments",
		       (String *) NULL, (Cardinal *)NULL);
	return False;
    }

    screen = *((Screen **) args[0].addr);
    colormap = *((Colormap *) args[1].addr);

    spec = (char *) fromVal->addr;
    if (strcasecmp (spec, XtDefaultForeground) == 0)
    {
	renderColor.red = 0;
	renderColor.green = 0;
	renderColor.blue = 0;
	renderColor.alpha = 0xffff;
    }
    else if (strcasecmp (spec, XtDefaultBackground) == 0)
    {
	renderColor.red = 0xffff;
	renderColor.green = 0xffff;
	renderColor.blue = 0xffff;
	renderColor.alpha = 0xffff;
    }
    else if (!XRenderParseColor (dpy, spec, &renderColor))
	return False;
    if (!XftColorAllocValue (dpy,
			     DefaultVisual (dpy,
					    XScreenNumberOfScreen (screen)),
			     colormap,
			     &renderColor,
			     &xftColor))
	return False;

    donestr (XftColor, xftColor, XtRXftColor);
}

static void
XmuFreeXftFont (XtAppContext app, XrmValuePtr toVal, XtPointer closure,
		XrmValuePtr args, Cardinal *num_args)
{
    Screen  *screen;
    XftFont *font;

    if (*num_args != 1)
    {
	XtAppErrorMsg (app,
		       "freeXftFont", "wrongParameters",
		       "XtToolkitError",
		       "Freeing an XftFont requires screen argument",
		       (String *) NULL, (Cardinal *)NULL);
	return;
    }

    screen = *((Screen **) args[0].addr);
    font = *((XftFont **) toVal->addr);
    if (font)
	XftFontClose (DisplayOfScreen (screen), font);
}

static Boolean
XmuCvtStringToXftFont(Display *dpy,
		      XrmValue *args, Cardinal *num_args,
		      XrmValue *fromVal, XrmValue *toVal,
		      XtPointer *converter_data)
{
    char    *name;
    XftFont *font;
    Screen  *screen;

    if (*num_args != 1)
    {
	XtAppErrorMsg (XtDisplayToApplicationContext (dpy),
		       "cvtStringToXftFont", "wrongParameters",
		       "XtToolkitError",
		       "String to XftFont conversion needs screen argument",
		       (String *) NULL, (Cardinal *)NULL);
	return False;
    }

    screen = *((Screen **) args[0].addr);
    name = (char *) fromVal->addr;

    font = XftFontOpenName (dpy,
			    XScreenNumberOfScreen (screen),
			    name);
    if (font)
    {
	donestr (XftFont *, font, XtRXftFont);
    }
    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRXftFont);
    return False;
}

static XtConvertArgRec xftFontConvertArgs[] = {
    {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.screen),
     sizeof(Screen *)},
};

#endif

static void
ClassInitialize(void)
{
#ifdef USE_XAW_PIXMAP_CVT
    XawInitializeWidgetSet();
#else
    static XtConvertArgRec scrnConvertArg[] = {
	{XtBaseOffset, (XtPointer) XtOffset(Widget, core.screen),
	 sizeof(Screen *)}
    };
    XtAddConverter( XtRString, XtRPixmap, CvtStringToPixmap,
	      	    scrnConvertArg, XtNumber(scrnConvertArg));
#endif
    XtAddConverter( XtRString, XtRBackingStore, XmuCvtStringToBackingStore,
		    NULL, 0 );
#ifdef XRENDER
    XtSetTypeConverter (XtRString, XtRXftColor,
			XmuCvtStringToXftColor,
			xftColorConvertArgs, XtNumber(xftColorConvertArgs),
			XtCacheByDisplay, XmuFreeXftColor);
    XtSetTypeConverter (XtRString, XtRXftFont,
			XmuCvtStringToXftFont,
			xftFontConvertArgs, XtNumber(xftFontConvertArgs),
			XtCacheByDisplay, XmuFreeXftFont);
#endif
}

static char *
TimeString (ClockWidget w, struct tm *tm)
{
   if (w->clock.brief)
   {
      if (w->clock.twentyfour)
      {
	  static char brief[6];
	  sprintf (brief, "%02d:%02d", tm->tm_hour, tm->tm_min);
	  return brief;
      }
      else
      {
	 static char brief[9];
	 int hour = tm->tm_hour % 12;
	 if (!hour) hour = 12;
	 sprintf (brief, "%02d:%02d %cM", hour, tm->tm_min,
	    tm->tm_hour >= 12 ? 'P' : 'A');
	 return brief;
      }
   }
   else if (w->clock.utime)
   {
      static char utime[35];
      Time_t tsec;
      tsec = time(NULL);
      sprintf (utime, "%10lu seconds since Epoch", (unsigned long)tsec);
      return utime;
   } else if (*w->clock.strftime) {
     /*Note: this code is probably excessively paranoid
       about buffer overflow.  The extra size 10 padding
       is also meant as a further guard against programmer
       error, although it is a little controversial*/
     static char ctime[STRFTIME_BUFF_SIZE+10];
     ctime[0] = ctime[STRFTIME_BUFF_SIZE] = '\0';
     if (0 < strftime (ctime, STRFTIME_BUFF_SIZE-1,w->clock.strftime, tm)) {
       ctime[STRFTIME_BUFF_SIZE-1] = '\0';
       return ctime;
     } else {
       return asctime (tm);
     }
   }
   else if (w->clock.twentyfour)
      return asctime (tm);
   else
   {
      static char long12[28];
      strftime(long12, sizeof long12, "%a %b %d %I:%M:%S %p %Y", tm);
      return long12;
   }
}

/* ARGSUSED */
static void
Initialize (Widget request, Widget new, ArgList args, Cardinal *num_args)
{
    ClockWidget w = (ClockWidget)new;
    XtGCMask		valuemask;
    XGCValues	myXGCV;
    int min_height, min_width;

    valuemask = GCForeground | GCBackground | GCFont | GCLineWidth;
    if (w->clock.font != NULL)
        myXGCV.font = w->clock.font->fid;
    else
        valuemask &= ~GCFont;	/* use server default font */

    min_width = min_height = ANALOG_SIZE_DEFAULT;
    if(!w->clock.analog) {
       char *str;
       struct tm tm;
       Time_t time_value;
       int len;

#ifndef NO_I18N
       w->clock.utf8 = False;

       if (!no_locale) {
	   char *time_locale = setlocale(LC_CTYPE, NULL);

	   if (strstr(time_locale, "UTF-8") || strstr(time_locale, "utf8")) {
	       w->clock.utf8 = True;
	   }

	   /*
	    * initialize time format from CFTIME if set, otherwise
	    * default to "%c".  This emulates ascftime, but we use
	    * strftime so we can limit the string buffer size to
	    * avoid possible buffer overflow.
	    */
	   if ((w->clock.strftime == NULL) || (w->clock.strftime[0] == 0)) {
	       w->clock.strftime = getenv("CFTIME");
	       if (w->clock.strftime == NULL) {
		   w->clock.strftime = "%c";
	       }
	   }
       }
#endif /* NO_I18N */

       (void) time(&time_value);
       tm = *localtime(&time_value);
       str = TimeString (w, &tm);
       len = strlen(str);
       if (len && str[len - 1] == '\n') str[--len] = '\0';

#ifdef XRENDER
       if (w->clock.render)
       {
	XGlyphInfo  extents;
#ifndef NO_I18N
# ifdef HAVE_ICONV
	char *utf8_str;
# endif
	if (w->clock.utf8)
	    XftTextExtentsUtf8 (XtDisplay (w), w->clock.face,
				(FcChar8 *) str, len, &extents);
# ifdef HAVE_ICONV
	else if ((utf8_str = clock_to_utf8(str, len)) != NULL) {
	        XftTextExtentsUtf8 (XtDisplay (w), w->clock.face,
			(FcChar8 *)utf8_str, strlen(utf8_str), &extents);
		free(utf8_str);
	}
# endif
	else
#endif
	    XftTextExtents8 (XtDisplay (w), w->clock.face,
			     (FcChar8 *) str, len, &extents);
	min_width = extents.xOff + 2 * w->clock.padding;
	min_height = w->clock.face->ascent + w->clock.face->descent +
		     2 * w->clock.padding;
        /*fprintf(stderr, "render min_width %i\n", min_width);*/
       }
       else
#endif
       { /* not XRENDER block */
#ifndef NO_I18N
	 if (!no_locale) {
	   XFontSetExtents *fse;

	   if(w->clock.fontSet == NULL) {
	       char **missing, *default_str;
	       int n_missing;
	       w->clock.fontSet = XCreateFontSet( XtDisplay(w),
		 XtDefaultFontSet,
		 &missing,
		 &n_missing,
		 &default_str);
	   }
	   if (w->clock.fontSet != NULL) {
	       /* don't free this... it's freed with the XFontSet. */
	       fse = XExtentsOfFontSet(w->clock.fontSet);

	       min_width = XmbTextEscapement(w->clock.fontSet,str,len) +
		 2 * w->clock.padding;
	       min_height = fse->max_logical_extent.height +
		 3 * w->clock.padding;
	       /*fprintf(stderr, "fontset min_width %i\n", min_width);*/
	   } else {
	       no_locale = True;
	   }
	 }

	 if (no_locale)
#endif /* NO_I18N */
	 {
	   if (w->clock.font == NULL)
	       w->clock.font = XQueryFont( XtDisplay(w),
					   XGContextFromGC(
					    DefaultGCOfScreen(XtScreen(w))) );
	   min_width = XTextWidth(w->clock.font, str, len) +
	       2 * w->clock.padding;
	   min_height = w->clock.font->ascent +
	       w->clock.font->descent + 2 * w->clock.padding;
	   /*fprintf(stderr, "font min_width %i\n", min_width);*/
	 }
       } /* not XRENDER block */
    }
    if (w->core.width == 0)
	w->core.width = min_width;
    if (w->core.height == 0)
	w->core.height = min_height;

    myXGCV.foreground = ClockFgPixel (w);
    myXGCV.background = w->core.background_pixel;
    if (w->clock.font != NULL)
	myXGCV.font = w->clock.font->fid;
    else
	valuemask &= ~GCFont;	/* use server default font */
    myXGCV.line_width = 0;
    w->clock.myGC = XtGetGC((Widget)w, valuemask, &myXGCV);

    valuemask = GCForeground | GCLineWidth | GCGraphicsExposures;
    myXGCV.foreground = w->core.background_pixel;
    if (w->core.background_pixmap != XtUnspecifiedPixmap) {
	myXGCV.tile = w->core.background_pixmap;
	myXGCV.fill_style = FillTiled;
	valuemask |= (GCTile | GCFillStyle);
    }
    myXGCV.graphics_exposures = False;
    w->clock.EraseGC = XtGetGC((Widget)w, valuemask, &myXGCV);
    valuemask &= ~(GCTile | GCFillStyle);

    myXGCV.foreground = w->clock.Hipixel;
    w->clock.HighGC = XtGetGC((Widget)w, valuemask, &myXGCV);

    valuemask = GCForeground;
    myXGCV.foreground = w->clock.Hdpixel;
    w->clock.HandGC = XtGetGC((Widget)w, valuemask, &myXGCV);

    /* make invalid update's use a default */
    /*if (w->clock.update <= 0) w->clock.update = 60;*/
    w->clock.show_second_hand = (abs(w->clock.update) <= SECOND_HAND_TIME);
    w->clock.numseg = 0;
    w->clock.interval_id = 0;
    memset (&w->clock.otm, '\0', sizeof (w->clock.otm));
#ifdef XRENDER
    {
	int major, minor;

	if (XRenderQueryVersion (XtDisplay (w), &major, &minor) &&
	    (major > 0 ||
	     (major == 0 && minor >= 4)))
	{
	    w->clock.can_polygon = True;
	}
	else
	    w->clock.can_polygon = False;
    }
    w->clock.pixmap = 0;
    w->clock.draw = NULL;
    w->clock.damage.x = 0;
    w->clock.damage.y = 0;
    w->clock.damage.height = 0;
    w->clock.damage.width = 0;
#endif
}

#ifdef XRENDER
static void
RenderPrepare (ClockWidget  w, XftColor *color)
{
    if (!w->clock.draw)
    {
	Drawable    d = XtWindow (w);
	if (w->clock.buffer)
	{
	    if (!w->clock.pixmap)
	    {
		Arg arg[1];
		w->clock.pixmap = XCreatePixmap (XtDisplay (w), d,
						 w->core.width,
						 w->core.height,
						 w->core.depth);
		arg[0].name = XtNbackgroundPixmap;
		arg[0].value = 0;
		XtSetValues ((Widget) w, arg, 1);
	    }
	    d = w->clock.pixmap;
	}

	w->clock.draw = XftDrawCreate (XtDisplay (w), d,
				       DefaultVisual (XtDisplay (w),
						      DefaultScreen(XtDisplay (w))),
				       w->core.colormap);
	w->clock.picture = XftDrawPicture (w->clock.draw);
    }
    if (color)
	w->clock.fill_picture = XftDrawSrcPicture (w->clock.draw, color);
}

static void
RenderClip (ClockWidget w)
{
    Region	r;
    Drawable	d;

    RenderPrepare (w, NULL);
    if (w->clock.buffer)
	d = w->clock.pixmap;
    else
	d = XtWindow (w);
    XFillRectangle (XtDisplay (w), d, w->clock.EraseGC,
		    w->clock.damage.x,
		    w->clock.damage.y,
		    w->clock.damage.width,
		    w->clock.damage.height);
    r = XCreateRegion ();
    XUnionRectWithRegion (&w->clock.damage,
			  r, r);
    XftDrawSetClip (w->clock.draw, r);
    XDestroyRegion (r);
}

static void
RenderTextBounds (ClockWidget w, char *str, int off, int len,
		  XRectangle *bounds, int *xp, int *yp)
{
    XGlyphInfo  head, tail;
    int	    x, y;

#ifndef NO_I18N
# ifdef HAVE_ICONV
    char *utf8_str;
# endif
    if (w->clock.utf8)
    {
	XftTextExtentsUtf8 (XtDisplay (w), w->clock.face,
			    (FcChar8 *) str, off, &head);
	XftTextExtentsUtf8 (XtDisplay (w), w->clock.face,
			    (FcChar8 *) str + off, len - off, &tail);
    }
# ifdef HAVE_ICONV
    else if ((utf8_str = clock_to_utf8(str, off)) != NULL)
    {
	XftTextExtentsUtf8 (XtDisplay (w), w->clock.face,
			    (FcChar8 *)utf8_str, strlen(utf8_str), &head);
	free(utf8_str);
	if ((utf8_str = clock_to_utf8(str+off, len-off)) != NULL) {
	  XftTextExtentsUtf8 (XtDisplay (w), w->clock.face,
			      (FcChar8 *)utf8_str, strlen(utf8_str), &tail);
	  free(utf8_str);
	} else
	  goto fallback;
    }
# endif
    else
#endif
    {
    fallback:
	XftTextExtents8 (XtDisplay (w), w->clock.face, (FcChar8 *) str,
			 off, &head);
	XftTextExtents8 (XtDisplay (w), w->clock.face, (FcChar8 *) str + off,
			 len - off, &tail);
    }

    /*
     * Compute position of tail
     */
    x = w->clock.padding + head.xOff;
    y = w->clock.face->ascent + w->clock.padding + head.yOff;
    /*
     * Compute bounds of tail, pad a bit as the bounds aren't exact
     */
    bounds->x = x - tail.x - 1;
    bounds->y = y - tail.y - 1;
    bounds->width = tail.width + 2;
    bounds->height = tail.height + 2;
    if (xp) *xp = x;
    if (yp) *yp = y;
}

static void
RenderUpdateRectBounds (XRectangle *damage, XRectangle *bounds)
{
    int	    x1 = bounds->x;
    int	    y1 = bounds->y;
    int	    x2 = bounds->x + bounds->width;
    int	    y2 = bounds->y + bounds->height;
    int	    d_x1 = damage->x;
    int	    d_y1 = damage->y;
    int	    d_x2 = damage->x + damage->width;
    int	    d_y2 = damage->y + damage->height;

    if (x1 == x2)
    {
	x1 = d_x1;
	x2 = d_x2;
    }
    else
    {
	if (d_x1 < x1) x1 = d_x1;
	if (d_x2 > x2) x2 = d_x2;
    }
    if (y1 == y2)
    {
	y1 = d_y1;
	y2 = d_y2;
    }
    else
    {
	if (d_y1 < y1) y1 = d_y1;
	if (d_y2 > y2) y2 = d_y2;
    }

    bounds->x = x1;
    bounds->y = y1;
    bounds->width = x2 - x1;
    bounds->height = y2 - y1;
}

static Boolean
RenderRectIn (XRectangle *rect, XRectangle *bounds)
{
    int	    x1 = bounds->x;
    int	    y1 = bounds->y;
    int	    x2 = bounds->x + bounds->width;
    int	    y2 = bounds->y + bounds->height;
    int	    r_x1 = rect->x;
    int	    r_y1 = rect->y;
    int	    r_x2 = rect->x + rect->width;
    int	    r_y2 = rect->y + rect->height;

    return r_x1 < x2 && x1 < r_x2 && r_y1 < y2 && y1 < r_y2;
}

#define LINE_WIDTH  0.01
#include <math.h>

#define XCoord(x,w) ((x) * (w)->clock.x_scale + (w)->clock.x_off)
#define YCoord(y,w) ((y) * (w)->clock.y_scale + (w)->clock.y_off)

static void
RenderUpdateBounds (XPointDouble *points, int npoints, XRectangle *bounds)
{
    int	    x1 = bounds->x;
    int	    y1 = bounds->y;
    int	    x2 = bounds->x + bounds->width;
    int	    y2 = bounds->y + bounds->height;

    while (npoints--)
    {
	int	    r_x1 = points[0].x;
	int	    r_y1 = points[0].y;
	int	    r_x2 = points[0].x + 1;
	int	    r_y2 = points[0].y + 1;

	if (x1 == x2)
	    x2 = x1 = r_x1;
	if (y1 == y2)
	    y2 = y1 = r_y1;
	if (r_x1 < x1) x1 = r_x1;
	if (r_y1 < y1) y1 = r_y1;
	if (r_x2 > x2) x2 = r_x2;
	if (r_y2 > y2) y2 = r_y2;
	points++;
    }
    bounds->x = x1;
    bounds->y = y1;
    bounds->width = x2 - x1;
    bounds->height = y2 - y1;
}

static Boolean
RenderCheckBounds (XPointDouble *points, int npoints, XRectangle *bounds)
{
    int	    x1 = bounds->x;
    int	    y1 = bounds->y;
    int	    x2 = bounds->x + bounds->width;
    int	    y2 = bounds->y + bounds->height;

    while (npoints--)
    {
	if (x1 <= points->x && points->x <= x2 &&
	    y1 <= points->y && points->y <= y2)
	    return True;
	points++;
    }
    return False;
}

static void
RenderUpdate (ClockWidget w)
{
    if (w->clock.buffer && w->clock.pixmap)
    {
	XCopyArea (XtDisplay (w), w->clock.pixmap,
		   XtWindow (w), w->clock.EraseGC,
		   w->clock.damage.x, w->clock.damage.y,
		   w->clock.damage.width, w->clock.damage.height,
		   w->clock.damage.x, w->clock.damage.y);
    }
}

static void
RenderResetBounds (XRectangle *bounds)
{
    bounds->x = 0;
    bounds->y = 0;
    bounds->width = 0;
    bounds->height = 0;
}

static void
RenderLine (ClockWidget w, XDouble x1, XDouble y1, XDouble x2, XDouble y2,
	    XftColor *color,
	    Boolean draw)
{
    XPointDouble    poly[4];
    XDouble	    dx = (x2 - x1);
    XDouble	    dy = (y2 - y1);
    XDouble	    len = sqrt (dx*dx + dy*dy);
    XDouble	    ldx = (LINE_WIDTH/2.0) * dy / len;
    XDouble	    ldy = (LINE_WIDTH/2.0) * dx / len;

    poly[0].x = XCoord (x1 + ldx, w);
    poly[0].y = YCoord (y1 - ldy, w);

    poly[1].x = XCoord (x2 + ldx, w);
    poly[1].y = YCoord (y2 - ldy, w);

    poly[2].x = XCoord (x2 - ldx, w);
    poly[2].y = YCoord (y2 + ldy, w);

    poly[3].x = XCoord (x1 - ldx, w);
    poly[3].y = YCoord (y1 + ldy, w);

    RenderUpdateBounds (poly, 4, &w->clock.damage);
    if (draw)
    {
	if (RenderCheckBounds (poly, 4, &w->clock.damage))
	{
	    RenderPrepare (w, color);
	    XRenderCompositeDoublePoly (XtDisplay (w),
					PictOpOver,
					w->clock.fill_picture,
					w->clock.picture,
					w->clock.mask_format,
					0, 0, 0, 0, poly, 4, EvenOddRule);
	}
    }
    else
	RenderUpdateBounds (poly, 4, &w->clock.damage);
}

static void
RenderRotate (ClockWidget w, XPointDouble *out, double x, double y, double s, double c)
{
    out->x = XCoord (x * c - y * s, w);
    out->y = YCoord (y * c + x * s, w);
}

static void
RenderHand (ClockWidget w, int tick_units, int size, XftColor *color,
	    Boolean draw)
{
    double	    c, s;
    XPointDouble    poly[3];
    double	    outer_x;
    double	    inner_y;

    ClockAngle (tick_units, &c, &s);
    s = -s;

    /* compute raw positions */
    outer_x = size / 100.0;
    inner_y = HAND_WIDTH_FRACT / 100.0;

    /* rotate them into position */
    RenderRotate (w, &poly[0], outer_x, 0.0, s, c);
    RenderRotate (w, &poly[1], -inner_y, inner_y, s, c);
    RenderRotate (w, &poly[2], -inner_y, -inner_y, s, c);

    if (draw)
    {
	if (RenderCheckBounds (poly, 3, &w->clock.damage))
	{
	    RenderPrepare (w, color);
	    XRenderCompositeDoublePoly (XtDisplay (w),
					PictOpOver,
					w->clock.fill_picture,
					w->clock.picture,
					w->clock.mask_format,
					0, 0, 0, 0, poly, 3, EvenOddRule);
	}
    }
    RenderUpdateBounds (poly, 3, &w->clock.damage);
}

static void
RenderHands (ClockWidget w, struct tm *tm, Boolean draw)
{
    RenderHand (w, tm->tm_hour * 300 + tm->tm_min*5, HOUR_HAND_FRACT, &w->clock.hour_color, draw);
    RenderHand (w, tm->tm_min * 60 + tm->tm_sec, MINUTE_HAND_FRACT, &w->clock.min_color, draw);
}

static void
RenderSec (ClockWidget w, struct tm *tm, Boolean draw)
{
    double	    c, s;
    XPointDouble    poly[10];
    double	    inner_x, middle_x, outer_x, far_x;
    double	    middle_y;
    double	    line_y;

    ClockAngle (tm->tm_sec * 60, &c, &s);

    s = -s;

    /*
     * Compute raw positions
     */
    line_y = LINE_WIDTH;
    inner_x = (MINUTE_HAND_FRACT / 100.0);
    middle_x = ((SECOND_HAND_FRACT + MINUTE_HAND_FRACT) / 200.0);
    outer_x = (SECOND_HAND_FRACT / 100.0);
    far_x = (MINOR_TICK_FRACT / 100.0);
    middle_y = (SECOND_WIDTH_FRACT / 100.0);

    /*
     * Rotate them into position
     */
    RenderRotate (w, &poly[0], -line_y, line_y, s, c);
    RenderRotate (w, &poly[1], inner_x, line_y, s, c);
    RenderRotate (w, &poly[2], middle_x, middle_y, s, c);
    RenderRotate (w, &poly[3], outer_x, line_y, s, c);
    RenderRotate (w, &poly[4], far_x, line_y, s, c);
    RenderRotate (w, &poly[5], far_x, -line_y, s, c);
    RenderRotate (w, &poly[6], outer_x, -line_y, s, c);
    RenderRotate (w, &poly[7], middle_x, -middle_y, s, c);
    RenderRotate (w, &poly[8], inner_x, -line_y, s, c);
    RenderRotate (w, &poly[9], -line_y, -line_y, s, c);

    if (draw)
    {
	if (RenderCheckBounds (poly, 10, &w->clock.damage))
	{
	    RenderPrepare (w, &w->clock.sec_color);
	    XRenderCompositeDoublePoly (XtDisplay (w),
					PictOpOver,
					w->clock.fill_picture,
					w->clock.picture,
					w->clock.mask_format,
					0, 0, 0, 0, poly, 10, EvenOddRule);
	}
    }
    else
    {
	RenderUpdateBounds (poly, 10, &w->clock.damage);
    }
}

#endif

static void
Realize(Widget gw, XtValueMask *valueMask, XSetWindowAttributes *attrs)
{
     ClockWidget	w = (ClockWidget) gw;
#ifdef notdef
     *valueMask |= CWBitGravity;
     attrs->bit_gravity = ForgetGravity;
#endif
     switch (w->clock.backing_store) {
     case Always:
     case NotUseful:
     case WhenMapped:
     	*valueMask |=CWBackingStore;
	attrs->backing_store = w->clock.backing_store;
	break;
     }
     (*clockWidgetClass->core_class.superclass->core_class.realize)
	 (gw, valueMask, attrs);
     Resize(gw);
}

static void
Destroy(Widget gw)
{
     ClockWidget w = (ClockWidget) gw;
     if (w->clock.interval_id) XtRemoveTimeOut (w->clock.interval_id);
#ifdef XRENDER
    if (w->clock.picture)
	XRenderFreePicture (XtDisplay(w), w->clock.picture);
    if (w->clock.fill_picture)
	XRenderFreePicture (XtDisplay(w), w->clock.fill_picture);
#endif
     XtReleaseGC (gw, w->clock.myGC);
     XtReleaseGC (gw, w->clock.HighGC);
     XtReleaseGC (gw, w->clock.HandGC);
     XtReleaseGC (gw, w->clock.EraseGC);
}

static void
Resize(Widget gw)
{
    ClockWidget w = (ClockWidget) gw;
    /* don't do this computation if window hasn't been realized yet. */
    if (XtIsRealized(gw) && w->clock.analog) {
	/* need signed value since Dimension is unsigned */
	int radius = ((int) min(w->core.width, w->core.height) - (int) (2 * w->clock.padding)) / 2;
        w->clock.radius = (Dimension) max (radius, 1);

        w->clock.second_hand_length = (int)(SECOND_HAND_FRACT * w->clock.radius) / 100;
        w->clock.minute_hand_length = (int)(MINUTE_HAND_FRACT * w->clock.radius) / 100;
        w->clock.hour_hand_length = (int)(HOUR_HAND_FRACT * w->clock.radius) / 100;
        w->clock.hand_width = (int)(HAND_WIDTH_FRACT * w->clock.radius) / 100;
        w->clock.second_hand_width = (int)(SECOND_WIDTH_FRACT * w->clock.radius) / 100;

        w->clock.centerX = w->core.width / 2;
        w->clock.centerY = w->core.height / 2;
    }
#ifdef XRENDER
    w->clock.x_scale = 0.45 * w->core.width;
    w->clock.y_scale = 0.45 * w->core.height;
    w->clock.x_off = 0.5 * w->core.width;
    w->clock.y_off = 0.5 * w->core.height;
    if (w->clock.pixmap)
    {
	XFreePixmap (XtDisplay (w), w->clock.pixmap);
	w->clock.pixmap = 0;
	if (w->clock.draw)
	{
	    XftDrawDestroy (w->clock.draw);
	    w->clock.draw = NULL;
	}
	w->clock.picture = 0;
    }
#endif
}

/* ARGSUSED */
static void
Redisplay(Widget gw, XEvent *event, Region region)
{
    ClockWidget w = (ClockWidget) gw;
    if (w->clock.analog) {
#ifdef XRENDER
	if (w->clock.render && w->clock.can_polygon)
	    XClipBox (region, &w->clock.damage);
	else
#endif
	{
	    if (w->clock.numseg != 0)
		erase_hands (w, (struct tm *) 0);
	    DrawClockFace(w);
	}
    } else {
#ifdef XRENDER
	if (w->clock.render)
	    XClipBox (region, &w->clock.damage);
#endif
	w->clock.prev_time_string[0] = '\0';
    }
    clock_tic((XtPointer)w, (XtIntervalId *)NULL);
}

/* Choose the update times for well-defined clock states.
 *
 * For example, in HH:MM:SS notation the last number rolls over
 * every 60 seconds and has at most 60 display states.  The sequence
 * depends on its initial value t0 and the update period u, e.g.
 *
 *   u (s)  d (s)  ti (s)                    m (states)  l (s)
 *    2      2     {0,2, .. 58}              30            60
 *    7      1     {0,7, .. 56,3, .. 53}     60           420
 *   15     15     {0,15,30,45}               4            60
 *   45     15     {0,45,30,15}               4           180
 *   53      1     {0,53,46, .. 4,57, .. 7}  60          3180
 *   58      2     {0,58,56, .. 2}           30          1740
 *   60     60     {0}                        1            60
 *
 * u=  update period in seconds,
 * ti= time at update i from the reference, HH:MM:00 or HH:00:00,
 * n=  the roll over time, the modulus, 60 s or 3600 s,
 * m=  the sequence length, the order of u in the modulo n group Z/nZ,
 * l=  the total sequence duration =m*u.
 * d=  gcd(n,u) the greatest common divisor
 *
 * The time t(i) determines the clock state.  It follows from
 *
 *   t(i)=t(i-1)+u mod n  <=>  t(i)=t(0)+i*u mod n
 *
 * which defines a { t(0) .. t(m-1) } sequence of m unique elements.
 * Hence, u generates a subgroup U={k*u mod n : k in Z} of Z/nZ so
 * its order m divides n.  This m satisfies
 *
 *   t(m)=t(0)  <=>  m*u mod n = 0  <=>  m*u = r*n  <=>  m=n/d, r=u/d
 *
 * where d divides n and u.  Choosing
 *
 *   d=gcd(n,u)  <=>  n/d and u/d are coprime  =>  m=n/d is minimum
 *
 * thus gives the order.  Furthermore, the greatest common divisor d is
 * also the minimum value generator of the set U.  Assume a generator e
 * where
 *
 *   e|{n,u}  <=>  Ai,Ej: i*u mod n = j*e  <=>  j=f(i)=(i*u mod n)/e
 *
 * such that f maps i to m=ord(u) unique values of j.  Its properties are
 *
 *   j=i*u/e mod n/e   ==>  0<=j<n/e
 *
 *   ord(u/e, mod n/e)=n/e/gcd(n/e,u/e)=n/d=m  ==>  J={j=f(i)}, |J|=m
 *
 *   ord(e)=n/gcd(n,e)=n/e
 *
 * from wich follows
 *
 *   e=d  ==>  f: I={0,..,m-1} -> J={0,..,m-1}: j=i*r mod m is bijective
 *        ==>  D={k*d mod n : k in Z} = U.
 *
 * Any value e below d is no generator since it yields a non contiguous
 * J such that an l=0..n/e-1 exists not in J with l*e not in U.
 *
 * The update sequence t(i) could be followed using the algorithm:
 *
 *   1. restore the expected value into t(i),
 *   2. calculate the next timeout t(i+1)=t(i)+u mod n,
 *   3. verify that the current tc(i) is between t(i)..t(i+1),
 *   4. calculate the time to wait w=t(i+1)-tc(i) mod n,
 *   5. store t(i+1),
 *
 * which implements state tracking.  This approach doesn't work well
 * since the set timeout w does not guarantee a next call at time
 * t(i+1), e.g. due to progam sleeps, time adjustments, and leap
 * seconds.  A robust method should only rely on the current time
 * tc(i) to identify t(i).  The derivation above shows 2 options:
 *
 *   1.  n={60,3600} and round to a multiple of d,
 *       but if d<u then the sequence is not guaranteed.
 *   2.  choose n large and round to a multiple of u,
 *       but then the sequence resets at roll-over.
 *
 * The code below implements (2) with n this year's duration in seconds
 * and using local time year's start as epoch.
 */
static unsigned long
waittime(int update, struct timeval *tv, struct tm *tm)
{
  int twait;
  long twaitms;
  unsigned long retval;

  if(update>0) {
    long tcur;
    int trem;

    tcur=tm->tm_sec+60*(tm->tm_min+60*(tm->tm_hour+24*tm->tm_yday));
    /* ti=floor(tcur/u)*u, w=u-(tcur-ti), and tcur-ti==tcur % u */
    trem=tcur % update;
    twait=update-trem;
  } else {
    twait=-update;
  }

  if(tv->tv_usec>0) {
    long usec;
    twait--;
    usec=1000000-tv->tv_usec;
    twaitms=(usec+999)/1000;  /* must round up to avoid zero retval */
  } else {
    twaitms=0;
  }

  retval=(unsigned long)labs(twaitms+1000*twait);
  return retval;
}

/* ARGSUSED */
static void
clock_tic(XtPointer client_data, XtIntervalId *id)
{
        ClockWidget w = (ClockWidget)client_data;
	struct tm tm;
	Time_t	time_value;
	struct timeval	tv;
	char	*time_ptr;
        register Display *dpy = XtDisplay(w);
        register Window win = XtWindow(w);

	X_GETTIMEOFDAY (&tv);
	time_value = tv.tv_sec;
	tm = *localtime(&time_value);
	if (w->clock.update && (id || !w->clock.interval_id))
	    w->clock.interval_id =
		XtAppAddTimeOut( XtWidgetToApplicationContext( (Widget) w),
				 waittime(w->clock.update, &tv, &tm),
				 clock_tic, (XtPointer)w );
	/*
	 * Beep on the half hour; double-beep on the hour.
	 */
	if (w->clock.chime == TRUE) {
	    if (w->clock.beeped && (tm.tm_min != 30) &&
		(tm.tm_min != 0))
	      w->clock.beeped = FALSE;
	    if (((tm.tm_min == 30) || (tm.tm_min == 0))
		&& (!w->clock.beeped)) {
		w->clock.beeped = TRUE;
#ifdef XKB
		if (tm.tm_min==0) {
		    XkbStdBell(dpy,win,50,XkbBI_ClockChimeHour);
		    XkbStdBell(dpy,win,50,XkbBI_RepeatingLastBell);
		}
		else {
		    XkbStdBell(dpy,win,50,XkbBI_ClockChimeHalf);
		}
#else
		XBell(dpy, 50);
		if (tm.tm_min == 0)
		  XBell(dpy, 50);
#endif
	    }
	}
	if( w->clock.analog == FALSE ) {
	    int	clear_from = w->core.width;
	    int i, len, prev_len;

	    time_ptr = TimeString (w, &tm);
	    len = strlen (time_ptr);
	    if (len && time_ptr[len - 1] == '\n') time_ptr[--len] = '\0';
	    prev_len = strlen (w->clock.prev_time_string);
	    for (i = 0; ((i < len) && (i < prev_len) &&
	    		 (w->clock.prev_time_string[i] == time_ptr[i])); i++);

#ifdef XRENDER
	    if (w->clock.render)
	    {
		XRectangle  old_tail, new_tail, head;
		int	    x, y;
#if !defined(NO_I18N) && defined(HAVE_ICONV)
		char *utf8_str;
#endif

		RenderTextBounds (w, w->clock.prev_time_string, i, prev_len,
				  &old_tail, NULL, NULL);
		RenderUpdateRectBounds (&old_tail, &w->clock.damage);
		RenderTextBounds (w, time_ptr, i, len,
				  &new_tail, NULL, NULL);
		RenderUpdateRectBounds (&new_tail, &w->clock.damage);

		while (i)
		{
		    RenderTextBounds (w, time_ptr, 0, i, &head, NULL, NULL);
		    if (!RenderRectIn (&head, &w->clock.damage))
			break;
		    i--;
		}
		RenderTextBounds (w, time_ptr, i, len, &new_tail, &x, &y);
		RenderClip (w);
		RenderPrepare (w, NULL);
#ifndef NO_I18N
		if (w->clock.utf8) {
		    XftDrawStringUtf8 (w->clock.draw,
				    &w->clock.fg_color,
				    w->clock.face,
				    x, y,
				    (FcChar8 *) time_ptr + i, len - i);

		}
# ifdef HAVE_ICONV
		else if ((utf8_str =
		    clock_to_utf8(time_ptr + i, len - i)) != NULL) {
		    	XftDrawStringUtf8 (w->clock.draw,
				    &w->clock.fg_color,
				    w->clock.face,
				    x, y,
				    (FcChar8 *)utf8_str, strlen(utf8_str) );
		    free(utf8_str);
		}
# endif
		else
#endif
		{
		    XftDrawString8 (w->clock.draw,
				    &w->clock.fg_color,
				    w->clock.face,
				    x, y,
				    (FcChar8 *) time_ptr + i, len - i);
		}
		RenderUpdate (w);
		RenderResetBounds (&w->clock.damage);
	    }
	    else
#endif
#ifndef NO_I18N
	    if(!no_locale) {
		if(0 < len) {
		    XFontSetExtents *fse
		      = XExtentsOfFontSet(w->clock.fontSet);

		    XmbDrawImageString(dpy,win,w->clock.fontSet,w->clock.myGC,
				       (2+w->clock.padding +
					(i?XmbTextEscapement(w->clock.fontSet,
							     time_ptr,i):0)),
				       2+w->clock.padding+fse->max_logical_extent.height,
				       time_ptr+i,len-i
			);
		    /*
		     * Clear any left over bits
		     */
		    clear_from = XmbTextEscapement (w->clock.fontSet,time_ptr,
						    len) + 2+w->clock.padding;
		}
	    } else
#endif /* NO_I18N */
	    {
		XDrawImageString (dpy, win, w->clock.myGC,
				  (1+w->clock.padding +
				   XTextWidth (w->clock.font, time_ptr, i)),
				  w->clock.font->ascent+w->clock.padding,
				  time_ptr + i, len - i);
		/*
		 * Clear any left over bits
		 */
		clear_from = XTextWidth (w->clock.font, time_ptr, len)
		    	     + 2 + w->clock.padding;
	    }
	    if (clear_from < (int)w->core.width)
		XClearArea (dpy, win,
		    clear_from, 0, w->core.width - clear_from, w->core.height,
		    False);
#ifdef HAVE_STRLCPY
	    strlcpy (w->clock.prev_time_string+i, time_ptr+i,
		     sizeof(w->clock.prev_time_string)-i);
#else
	    strncpy (w->clock.prev_time_string+i, time_ptr+i,
		     sizeof(w->clock.prev_time_string)-i);
	    w->clock.prev_time_string[sizeof(w->clock.prev_time_string)-1] = 0;
#endif
	} else {
			/*
			 * The second (or minute) hand is sec (or min)
			 * sixtieths around the clock face. The hour hand is
			 * (hour + min/60) twelfths of the way around the
			 * clock-face.  The derivation is left as an excercise
			 * for the reader.
			 */

			/*
			 * 12 hour clock.
			 */
			if(tm.tm_hour >= 12)
				tm.tm_hour -= 12;

#ifdef XRENDER
			if (w->clock.render && w->clock.can_polygon)
			{
			    w->clock.mask_format = XRenderFindStandardFormat (XtDisplay (w),
									      w->clock.sharp ?
									      PictStandardA1 :
									      PictStandardA8);
			    /*
			     * Compute repaint area
			     */
			    if (tm.tm_min != w->clock.otm.tm_min ||
				tm.tm_hour != w->clock.otm.tm_hour ||
				tm.tm_sec != w->clock.otm.tm_sec)
			    {
				RenderHands (w, &w->clock.otm, False);
				RenderHands (w, &tm, False);
			    }
			    if (w->clock.show_second_hand &&
				tm.tm_sec != w->clock.otm.tm_sec)
			    {
				RenderSec (w, &w->clock.otm, False);
				RenderSec (w, &tm, False);
			    }
			    if (w->clock.damage.width &&
				w->clock.damage.height)
			    {
				RenderClip (w);
				DrawClockFace (w);
				RenderHands (w, &tm, True);
				if (w->clock.show_second_hand == TRUE)
				    RenderSec (w, &tm, True);
			    }
			    w->clock.otm = tm;
			    RenderUpdate (w);
			    RenderResetBounds (&w->clock.damage);
			    return;
			}
#endif

			erase_hands (w, &tm);

		    if (w->clock.numseg == 0 ||
			tm.tm_min != w->clock.otm.tm_min ||
			tm.tm_hour != w->clock.otm.tm_hour) {
			    w->clock.segbuffptr = w->clock.segbuff;
			    w->clock.numseg = 0;
			    /*
			     * Calculate the hour hand, fill it in with its
			     * color and then outline it.  Next, do the same
			     * with the minute hand.  This is a cheap hidden
			     * line algorithm.
			     */
			    DrawHand(w,
				w->clock.minute_hand_length, w->clock.hand_width,
				tm.tm_min * 60
			    );
			    if(w->clock.Hdpixel != w->core.background_pixel)
				XFillPolygon( dpy,
				    win, w->clock.HandGC,
				    w->clock.segbuff, VERTICES_IN_HANDS,
				    Convex, CoordModeOrigin
				);
			    XDrawLines( dpy,
				win, w->clock.HighGC,
				w->clock.segbuff, VERTICES_IN_HANDS,
				       CoordModeOrigin);
			    w->clock.hour = w->clock.segbuffptr;
			    DrawHand(w,
				w->clock.hour_hand_length, w->clock.hand_width,
				tm.tm_hour * 300 + tm.tm_min * 5
			    );
			    if(w->clock.Hdpixel != w->core.background_pixel) {
			      XFillPolygon(dpy,
					   win, w->clock.HandGC,
					   w->clock.hour,
					   VERTICES_IN_HANDS,
					   Convex, CoordModeOrigin
					   );
			    }
			    XDrawLines( dpy,
				       win, w->clock.HighGC,
				       w->clock.hour, VERTICES_IN_HANDS,
				       CoordModeOrigin );

			    w->clock.sec = w->clock.segbuffptr;
		    }
		    if (w->clock.show_second_hand == TRUE) {
			    w->clock.segbuffptr = w->clock.sec;
			    DrawSecond(w,
				w->clock.second_hand_length - 2,
				w->clock.second_hand_width,
				w->clock.minute_hand_length + 2,
				tm.tm_sec * 60
			    );
			    if(w->clock.Hdpixel != w->core.background_pixel)
				XFillPolygon( dpy,
				    win, w->clock.HandGC,
				    w->clock.sec,
				    VERTICES_IN_HANDS -2,
				    Convex, CoordModeOrigin
			    );
			    XDrawLines( dpy,
				       win, w->clock.HighGC,
				       w->clock.sec,
				       VERTICES_IN_HANDS-1,
				       CoordModeOrigin
				        );

			}
			w->clock.otm = tm;
		}
}

static void
erase_hands(ClockWidget w, struct tm *tm)
{
    /*
     * Erase old hands.
     */
    if(w->clock.numseg > 0) {
	Display	*dpy;
	Window	win;

	dpy = XtDisplay (w);
	win = XtWindow (w);
	if (w->clock.show_second_hand == TRUE) {
	    XDrawLines(dpy, win,
		w->clock.EraseGC,
		w->clock.sec,
		VERTICES_IN_HANDS-1,
		CoordModeOrigin);
	    if(w->clock.Hdpixel != w->core.background_pixel) {
		XFillPolygon(dpy,
			win, w->clock.EraseGC,
			w->clock.sec,
			VERTICES_IN_HANDS-2,
			Convex, CoordModeOrigin
			);
	    }
	}
	if(!tm || tm->tm_min != w->clock.otm.tm_min ||
		  tm->tm_hour != w->clock.otm.tm_hour)
 	{
	    XDrawLines( dpy, win,
			w->clock.EraseGC,
			w->clock.segbuff,
			VERTICES_IN_HANDS,
			CoordModeOrigin);
	    XDrawLines( dpy, win,
			w->clock.EraseGC,
			w->clock.hour,
			VERTICES_IN_HANDS,
			CoordModeOrigin);
	    if(w->clock.Hdpixel != w->core.background_pixel) {
		XFillPolygon( dpy, win,
 			      w->clock.EraseGC,
			      w->clock.segbuff, VERTICES_IN_HANDS,
			      Convex, CoordModeOrigin);
		XFillPolygon( dpy, win,
 			      w->clock.EraseGC,
			      w->clock.hour,
			      VERTICES_IN_HANDS,
			      Convex, CoordModeOrigin);
	    }
	}
    }
}

static double const Sines[] = {
0.000000, 0.001745, 0.003490, 0.005235, 0.006981, 0.008726, 0.010471, 0.012217,
0.013962, 0.015707, 0.017452, 0.019197, 0.020942, 0.022687, 0.024432, 0.026176,
0.027921, 0.029666, 0.031410, 0.033155, 0.034899, 0.036643, 0.038387, 0.040131,
0.041875, 0.043619, 0.045362, 0.047106, 0.048849, 0.050592, 0.052335, 0.054078,
0.055821, 0.057564, 0.059306, 0.061048, 0.062790, 0.064532, 0.066273, 0.068015,
0.069756, 0.071497, 0.073238, 0.074978, 0.076719, 0.078459, 0.080198, 0.081938,
0.083677, 0.085416, 0.087155, 0.088894, 0.090632, 0.092370, 0.094108, 0.095845,
0.097582, 0.099319, 0.101056, 0.102792, 0.104528, 0.106264, 0.107999, 0.109734,
0.111468, 0.113203, 0.114937, 0.116670, 0.118403, 0.120136, 0.121869, 0.123601,
0.125333, 0.127064, 0.128795, 0.130526, 0.132256, 0.133986, 0.135715, 0.137444,
0.139173, 0.140901, 0.142628, 0.144356, 0.146083, 0.147809, 0.149535, 0.151260,
0.152985, 0.154710, 0.156434, 0.158158, 0.159881, 0.161603, 0.163325, 0.165047,
0.166768, 0.168489, 0.170209, 0.171929, 0.173648, 0.175366, 0.177084, 0.178802,
0.180519, 0.182235, 0.183951, 0.185666, 0.187381, 0.189095, 0.190808, 0.192521,
0.194234, 0.195946, 0.197657, 0.199367, 0.201077, 0.202787, 0.204496, 0.206204,
0.207911, 0.209618, 0.211324, 0.213030, 0.214735, 0.216439, 0.218143, 0.219846,
0.221548, 0.223250, 0.224951, 0.226651, 0.228350, 0.230049, 0.231747, 0.233445,
0.235142, 0.236838, 0.238533, 0.240228, 0.241921, 0.243615, 0.245307, 0.246999,
0.248689, 0.250380, 0.252069, 0.253757, 0.255445, 0.257132, 0.258819, 0.260504,
0.262189, 0.263873, 0.265556, 0.267238, 0.268919, 0.270600, 0.272280, 0.273959,
0.275637, 0.277314, 0.278991, 0.280666, 0.282341, 0.284015, 0.285688, 0.287360,
0.289031, 0.290702, 0.292371, 0.294040, 0.295708, 0.297374, 0.299040, 0.300705,
0.302369, 0.304033, 0.305695, 0.307356, 0.309016, 0.310676, 0.312334, 0.313992,
0.315649, 0.317304, 0.318959, 0.320612, 0.322265, 0.323917, 0.325568, 0.327217,
0.328866, 0.330514, 0.332161, 0.333806, 0.335451, 0.337095, 0.338737, 0.340379,
0.342020, 0.343659, 0.345298, 0.346935, 0.348572, 0.350207, 0.351841, 0.353474,
0.355106, 0.356737, 0.358367, 0.359996, 0.361624, 0.363251, 0.364876, 0.366501,
0.368124, 0.369746, 0.371367, 0.372987, 0.374606, 0.376224, 0.377840, 0.379456,
0.381070, 0.382683, 0.384295, 0.385906, 0.387515, 0.389123, 0.390731, 0.392337,
0.393941, 0.395545, 0.397147, 0.398749, 0.400349, 0.401947, 0.403545, 0.405141,
0.406736, 0.408330, 0.409923, 0.411514, 0.413104, 0.414693, 0.416280, 0.417867,
0.419452, 0.421035, 0.422618, 0.424199, 0.425779, 0.427357, 0.428935, 0.430511,
0.432085, 0.433659, 0.435231, 0.436801, 0.438371, 0.439939, 0.441505, 0.443071,
0.444635, 0.446197, 0.447759, 0.449318, 0.450877, 0.452434, 0.453990, 0.455544,
0.457097, 0.458649, 0.460199, 0.461748, 0.463296, 0.464842, 0.466386, 0.467929,
0.469471, 0.471011, 0.472550, 0.474088, 0.475624, 0.477158, 0.478691, 0.480223,
0.481753, 0.483282, 0.484809, 0.486335, 0.487859, 0.489382, 0.490903, 0.492423,
0.493941, 0.495458, 0.496973, 0.498487, 0.499999, 0.501510, 0.503019, 0.504527,
0.506033, 0.507538, 0.509041, 0.510542, 0.512042, 0.513541, 0.515038, 0.516533,
0.518027, 0.519519, 0.521009, 0.522498, 0.523985, 0.525471, 0.526955, 0.528438,
0.529919, 0.531398, 0.532876, 0.534352, 0.535826, 0.537299, 0.538770, 0.540240,
0.541708, 0.543174, 0.544639, 0.546101, 0.547563, 0.549022, 0.550480, 0.551936,
0.553391, 0.554844, 0.556295, 0.557745, 0.559192, 0.560638, 0.562083, 0.563526,
0.564967, 0.566406, 0.567843, 0.569279, 0.570713, 0.572145, 0.573576, 0.575005,
0.576432, 0.577857, 0.579281, 0.580702, 0.582122, 0.583541, 0.584957, 0.586372,
0.587785, 0.589196, 0.590605, 0.592013, 0.593418, 0.594822, 0.596224, 0.597625,
0.599023, 0.600420, 0.601815, 0.603207, 0.604599, 0.605988, 0.607375, 0.608761,
0.610145, 0.611527, 0.612907, 0.614285, 0.615661, 0.617035, 0.618408, 0.619779,
0.621147, 0.622514, 0.623879, 0.625242, 0.626603, 0.627963, 0.629320, 0.630675,
0.632029, 0.633380, 0.634730, 0.636078, 0.637423, 0.638767, 0.640109, 0.641449,
0.642787, 0.644123, 0.645457, 0.646789, 0.648119, 0.649448, 0.650774, 0.652098,
0.653420, 0.654740, 0.656059, 0.657375, 0.658689, 0.660001, 0.661311, 0.662620,
0.663926, 0.665230, 0.666532, 0.667832, 0.669130, 0.670426, 0.671720, 0.673012,
0.674302, 0.675590, 0.676875, 0.678159, 0.679441, 0.680720, 0.681998, 0.683273,
0.684547, 0.685818, 0.687087, 0.688354, 0.689619, 0.690882, 0.692143, 0.693401,
0.694658, 0.695912, 0.697165, 0.698415, 0.699663, 0.700909, 0.702153, 0.703394,
0.704634, 0.705871, 0.707106,
};
static double const Cosines[] = {
1.000000, 0.999998, 0.999993, 0.999986, 0.999975, 0.999961, 0.999945, 0.999925,
0.999902, 0.999876, 0.999847, 0.999815, 0.999780, 0.999742, 0.999701, 0.999657,
0.999610, 0.999559, 0.999506, 0.999450, 0.999390, 0.999328, 0.999262, 0.999194,
0.999122, 0.999048, 0.998970, 0.998889, 0.998806, 0.998719, 0.998629, 0.998536,
0.998440, 0.998341, 0.998239, 0.998134, 0.998026, 0.997915, 0.997801, 0.997684,
0.997564, 0.997440, 0.997314, 0.997185, 0.997052, 0.996917, 0.996778, 0.996637,
0.996492, 0.996345, 0.996194, 0.996041, 0.995884, 0.995724, 0.995561, 0.995396,
0.995227, 0.995055, 0.994880, 0.994702, 0.994521, 0.994337, 0.994150, 0.993960,
0.993767, 0.993571, 0.993372, 0.993170, 0.992965, 0.992757, 0.992546, 0.992331,
0.992114, 0.991894, 0.991671, 0.991444, 0.991215, 0.990983, 0.990747, 0.990509,
0.990268, 0.990023, 0.989776, 0.989525, 0.989272, 0.989015, 0.988756, 0.988493,
0.988228, 0.987959, 0.987688, 0.987413, 0.987136, 0.986855, 0.986572, 0.986285,
0.985996, 0.985703, 0.985407, 0.985109, 0.984807, 0.984503, 0.984195, 0.983885,
0.983571, 0.983254, 0.982935, 0.982612, 0.982287, 0.981958, 0.981627, 0.981292,
0.980955, 0.980614, 0.980271, 0.979924, 0.979575, 0.979222, 0.978867, 0.978508,
0.978147, 0.977783, 0.977415, 0.977045, 0.976672, 0.976296, 0.975916, 0.975534,
0.975149, 0.974761, 0.974370, 0.973975, 0.973578, 0.973178, 0.972775, 0.972369,
0.971961, 0.971549, 0.971134, 0.970716, 0.970295, 0.969872, 0.969445, 0.969015,
0.968583, 0.968147, 0.967709, 0.967267, 0.966823, 0.966376, 0.965925, 0.965472,
0.965016, 0.964557, 0.964095, 0.963630, 0.963162, 0.962691, 0.962217, 0.961741,
0.961261, 0.960779, 0.960293, 0.959805, 0.959313, 0.958819, 0.958322, 0.957822,
0.957319, 0.956813, 0.956304, 0.955793, 0.955278, 0.954760, 0.954240, 0.953716,
0.953190, 0.952661, 0.952129, 0.951594, 0.951056, 0.950515, 0.949972, 0.949425,
0.948876, 0.948323, 0.947768, 0.947210, 0.946649, 0.946085, 0.945518, 0.944948,
0.944376, 0.943800, 0.943222, 0.942641, 0.942057, 0.941470, 0.940880, 0.940288,
0.939692, 0.939094, 0.938493, 0.937888, 0.937281, 0.936672, 0.936059, 0.935444,
0.934825, 0.934204, 0.933580, 0.932953, 0.932323, 0.931691, 0.931055, 0.930417,
0.929776, 0.929132, 0.928485, 0.927836, 0.927183, 0.926528, 0.925870, 0.925209,
0.924546, 0.923879, 0.923210, 0.922538, 0.921863, 0.921185, 0.920504, 0.919821,
0.919135, 0.918446, 0.917754, 0.917060, 0.916362, 0.915662, 0.914959, 0.914253,
0.913545, 0.912834, 0.912120, 0.911403, 0.910683, 0.909961, 0.909236, 0.908508,
0.907777, 0.907044, 0.906307, 0.905568, 0.904827, 0.904082, 0.903335, 0.902585,
0.901832, 0.901077, 0.900318, 0.899557, 0.898794, 0.898027, 0.897258, 0.896486,
0.895711, 0.894934, 0.894154, 0.893371, 0.892585, 0.891797, 0.891006, 0.890212,
0.889416, 0.888617, 0.887815, 0.887010, 0.886203, 0.885393, 0.884580, 0.883765,
0.882947, 0.882126, 0.881303, 0.880477, 0.879648, 0.878817, 0.877982, 0.877146,
0.876306, 0.875464, 0.874619, 0.873772, 0.872922, 0.872069, 0.871213, 0.870355,
0.869494, 0.868631, 0.867765, 0.866896, 0.866025, 0.865151, 0.864274, 0.863395,
0.862513, 0.861629, 0.860742, 0.859852, 0.858959, 0.858064, 0.857167, 0.856267,
0.855364, 0.854458, 0.853550, 0.852640, 0.851726, 0.850811, 0.849892, 0.848971,
0.848048, 0.847121, 0.846193, 0.845261, 0.844327, 0.843391, 0.842452, 0.841510,
0.840566, 0.839619, 0.838670, 0.837718, 0.836764, 0.835807, 0.834847, 0.833885,
0.832921, 0.831954, 0.830984, 0.830012, 0.829037, 0.828060, 0.827080, 0.826098,
0.825113, 0.824126, 0.823136, 0.822144, 0.821149, 0.820151, 0.819152, 0.818149,
0.817144, 0.816137, 0.815127, 0.814115, 0.813100, 0.812083, 0.811063, 0.810041,
0.809016, 0.807989, 0.806960, 0.805928, 0.804893, 0.803856, 0.802817, 0.801775,
0.800731, 0.799684, 0.798635, 0.797583, 0.796529, 0.795473, 0.794414, 0.793353,
0.792289, 0.791223, 0.790155, 0.789084, 0.788010, 0.786935, 0.785856, 0.784776,
0.783693, 0.782608, 0.781520, 0.780430, 0.779337, 0.778243, 0.777145, 0.776046,
0.774944, 0.773840, 0.772733, 0.771624, 0.770513, 0.769399, 0.768283, 0.767165,
0.766044, 0.764921, 0.763796, 0.762668, 0.761538, 0.760405, 0.759271, 0.758134,
0.756995, 0.755853, 0.754709, 0.753563, 0.752414, 0.751264, 0.750111, 0.748955,
0.747798, 0.746638, 0.745475, 0.744311, 0.743144, 0.741975, 0.740804, 0.739631,
0.738455, 0.737277, 0.736097, 0.734914, 0.733729, 0.732542, 0.731353, 0.730162,
0.728968, 0.727772, 0.726574, 0.725374, 0.724171, 0.722967, 0.721760, 0.720551,
0.719339, 0.718126, 0.716910, 0.715692, 0.714472, 0.713250, 0.712026, 0.710799,
0.709570, 0.708339, 0.707106,
};

static void
ClockAngle(int tick_units, double *sinp, double *cosp)
{
    int reduced, upper;

    reduced = tick_units % 450;
    upper = tick_units / 450;
    if (upper & 1)
	reduced = 450 - reduced;
    if ((upper + 1) & 2) {
	*sinp = Cosines[reduced];
	*cosp = Sines[reduced];
    } else {
	*sinp = Sines[reduced];
	*cosp = Cosines[reduced];
    }
    if (upper >= 2 && upper < 6)
	*cosp = -*cosp;
    if (upper >= 4)
	*sinp = -*sinp;
}

/*
 * DrawLine - Draws a line.
 *
 * blank_length is the distance from the center which the line begins.
 * length is the maximum length of the hand.
 * Tick_units is a number between zero and 12*60 indicating
 * how far around the circle (clockwise) from high noon.
 *
 * The blank_length feature is because I wanted to draw tick-marks around the
 * circle (for seconds).  The obvious means of drawing lines from the center
 * to the perimeter, then erasing all but the outside most pixels doesn't
 * work because of round-off error (sigh).
 */
static void
DrawLine(ClockWidget w, Dimension blank_length, Dimension length,
	 int tick_units)
{
	double dblank_length = (double)blank_length, dlength = (double)length;
	double cosangle, sinangle;
	int cx = w->clock.centerX, cy = w->clock.centerY, x1, y1, x2, y2;

	/*
	 *  Angles are measured from 12 o'clock, clockwise increasing.
	 *  Since in X, +x is to the right and +y is downward:
	 *
	 *	x = x0 + r * sin(theta)
	 *	y = y0 - r * cos(theta)
	 *
	 */
	ClockAngle(tick_units, &sinangle, &cosangle);

	/* break this out so that stupid compilers can cope */
	x1 = cx + (int)(dblank_length * sinangle);
	y1 = cy - (int)(dblank_length * cosangle);
	x2 = cx + (int)(dlength * sinangle);
	y2 = cy - (int)(dlength * cosangle);
	SetSeg(w, x1, y1, x2, y2);
}

/*
 * DrawHand - Draws a hand.
 *
 * length is the maximum length of the hand.
 * width is the half-width of the hand.
 * Tick_units is a number between zero and 12*60 indicating
 * how far around the circle (clockwise) from high noon.
 *
 */
static void
DrawHand(ClockWidget w, Dimension length, Dimension width, int tick_units)
{

	double cosangle, sinangle;
	register double ws, wc;
	Position x, y, x1, y1, x2, y2;

	/*
	 *  Angles are measured from 12 o'clock, clockwise increasing.
	 *  Since in X, +x is to the right and +y is downward:
	 *
	 *	x = x0 + r * sin(theta)
	 *	y = y0 - r * cos(theta)
	 *
	 */
	ClockAngle(tick_units, &sinangle, &cosangle);
	/*
	 * Order of points when drawing the hand.
	 *
	 *		1,4
	 *		/ \
	 *	       /   \
	 *	      /     \
	 *	    2 ------- 3
	 */
	wc = width * cosangle;
	ws = width * sinangle;
	SetSeg(w,
	       x = w->clock.centerX + clock_round(length * sinangle),
	       y = w->clock.centerY - clock_round(length * cosangle),
	       x1 = w->clock.centerX - clock_round(ws + wc),
	       y1 = w->clock.centerY + clock_round(wc - ws));  /* 1 ---- 2 */
	/* 2 */
	SetSeg(w, x1, y1,
	       x2 = w->clock.centerX - clock_round(ws - wc),
	       y2 = w->clock.centerY + clock_round(wc + ws));  /* 2 ----- 3 */

	SetSeg(w, x2, y2, x, y);	/* 3 ----- 1(4) */
}

/*
 * DrawSecond - Draws the second hand (diamond).
 *
 * length is the maximum length of the hand.
 * width is the half-width of the hand.
 * offset is direct distance from center to tail end.
 * Tick_units is a number between zero and 12*60 indicating
 * how far around the circle (clockwise) from high noon.
 *
 */
static void
DrawSecond(ClockWidget w, Dimension length, Dimension width,
	   Dimension offset, int tick_units)
{

	double cosangle, sinangle;
	register double ms, mc, ws, wc;
	register int mid;
	Position x, y;

	/*
	 *  Angles are measured from 12 o'clock, clockwise increasing.
	 *  Since in X, +x is to the right and +y is downward:
	 *
	 *	x = x0 + r * sin(theta)
	 *	y = y0 - r * cos(theta)
	 *
	 */
	ClockAngle(tick_units, &sinangle, &cosangle);
	/*
	 * Order of points when drawing the hand.
	 *
	 *		1,5
	 *		/ \
	 *	       /   \
	 *	      /     \
	 *	    2<       >4
	 *	      \     /
	 *	       \   /
	 *		\ /
	 *	-	 3
	 *	|
	 *	|
	 *   offset
	 *	|
	 *	|
	 *	-	 + center
	 */

	mid = (int) (length + offset) / 2;
	mc = mid * cosangle;
	ms = mid * sinangle;
	wc = width * cosangle;
	ws = width * sinangle;
	/*1 ---- 2 */
	SetSeg(w,
	       x = w->clock.centerX + clock_round(length * sinangle),
	       y = w->clock.centerY - clock_round(length * cosangle),
	       w->clock.centerX + clock_round(ms - wc),
	       w->clock.centerY - clock_round(mc + ws) );
	SetSeg(w, w->clock.centerX + clock_round(offset *sinangle),
	       w->clock.centerY - clock_round(offset * cosangle), /* 2-----3 */
	       w->clock.centerX + clock_round(ms + wc),
	       w->clock.centerY - clock_round(mc - ws));
	w->clock.segbuffptr->x = x;
	w->clock.segbuffptr++->y = y;
	w->clock.numseg ++;
}

static void
SetSeg(ClockWidget w, int x1, int y1, int x2, int y2)
{
	w->clock.segbuffptr->x = x1;
	w->clock.segbuffptr++->y = y1;
	w->clock.segbuffptr->x = x2;
	w->clock.segbuffptr++->y = y2;
	w->clock.numseg += 2;
}

/*
 *  Draw the clock face (every fifth tick-mark is longer
 *  than the others).
 */
static void
DrawClockFace(ClockWidget w)
{
	register int i;
	register int delta = (int)(w->clock.radius - w->clock.second_hand_length) / 3;

	w->clock.segbuffptr = w->clock.segbuff;
	w->clock.numseg = 0;
	for (i = 0; i < 60; i++)
	{
#ifdef XRENDER
	    if (w->clock.render && w->clock.can_polygon)
	    {
		double	s, c;
		XDouble	x1, y1, x2, y2;
		XftColor	*color;
		ClockAngle (i * 60, &s, &c);
		x1 = c;
		y1 = s;
		if (i % 5)
		{
		    x2 = c * (MINOR_TICK_FRACT / 100.0);
		    y2 = s * (MINOR_TICK_FRACT / 100.0);
		    color = &w->clock.minor_color;
		}
		else
		{
		    x2 = c * (SECOND_HAND_FRACT / 100.0);
		    y2 = s * (SECOND_HAND_FRACT / 100.0);
		    color = &w->clock.major_color;
		}
		RenderLine (w, x1, y1, x2, y2, color, True);
	    }
	    else
#endif
	    {
		DrawLine(w, ( (i % 5) == 0 ?
			     w->clock.second_hand_length :
			     (w->clock.radius - delta) ),
			 w->clock.radius, i * 60);
	    }
	}
#ifdef XRENDER
	if (w->clock.render && w->clock.can_polygon)
	    return;
#endif
	/*
	 * Go ahead and draw it.
	 */
	XDrawSegments(XtDisplay(w), XtWindow(w),
		      w->clock.myGC, (XSegment *) &(w->clock.segbuff[0]),
		      w->clock.numseg/2);

	w->clock.segbuffptr = w->clock.segbuff;
	w->clock.numseg = 0;
}

static int
clock_round(double x)
{
	return(x >= 0.0 ? (int)(x + .5) : (int)(x - .5));
}

#ifdef XRENDER
static Boolean
sameColor (XftColor *old, XftColor *new)
{
    if (old->color.red != new->color.red) return False;
    if (old->color.green != new->color.green) return False;
    if (old->color.blue != new->color.blue) return False;
    if (old->color.alpha != new->color.alpha) return False;
    return True;
}
#endif

/* ARGSUSED */
static Boolean
SetValues(Widget gcurrent, Widget grequest, Widget gnew,
	  ArgList args, Cardinal *num_args)
{
      ClockWidget current = (ClockWidget) gcurrent;
      ClockWidget new = (ClockWidget) gnew;
      Boolean redisplay = FALSE;
      XtGCMask valuemask;
      XGCValues	myXGCV;

      /* first check for changes to clock-specific resources.  We'll accept all
         the changes, but may need to do some computations first. */

      if (new->clock.update != current->clock.update) {
	  if (current->clock.interval_id)
	      XtRemoveTimeOut (current->clock.interval_id);
	  if (new->clock.update && XtIsRealized( (Widget) new))
	      new->clock.interval_id = XtAppAddTimeOut(
                                         XtWidgetToApplicationContext(gnew),
					 abs(new->clock.update)*1000,
				         clock_tic, (XtPointer)gnew);

	  new->clock.show_second_hand =(abs(new->clock.update) <= SECOND_HAND_TIME);
	  if (new->clock.show_second_hand != current->clock.show_second_hand)
	    redisplay = TRUE;
      }

      if (new->clock.padding != current->clock.padding)
	   redisplay = TRUE;

      if (new->clock.analog != current->clock.analog)
	   redisplay = TRUE;

       if (new->clock.font != current->clock.font)
	   redisplay = TRUE;

#ifndef NO_I18N
       if (new->clock.fontSet != current->clock.fontSet)
	   redisplay = TRUE;
#endif

      if ((ClockFgPixel(new) != ClockFgPixel (current))
          || (new->core.background_pixel != current->core.background_pixel)) {
          valuemask = GCForeground | GCBackground | GCFont | GCLineWidth;
	  myXGCV.foreground = ClockFgPixel (new);
	  myXGCV.background = new->core.background_pixel;
          myXGCV.font = new->clock.font->fid;
	  myXGCV.line_width = 0;
	  XtReleaseGC (gcurrent, current->clock.myGC);
	  new->clock.myGC = XtGetGC(gcurrent, valuemask, &myXGCV);
	  redisplay = TRUE;
          }

      if (new->clock.Hipixel != current->clock.Hipixel) {
          valuemask = GCForeground | GCLineWidth;
	  myXGCV.foreground = new->clock.Hipixel;
          myXGCV.font = new->clock.font->fid;
	  myXGCV.line_width = 0;
	  XtReleaseGC (gcurrent, current->clock.HighGC);
	  new->clock.HighGC = XtGetGC((Widget)gcurrent, valuemask, &myXGCV);
	  redisplay = TRUE;
          }

      if (new->clock.Hdpixel != current->clock.Hdpixel) {
          valuemask = GCForeground;
	  myXGCV.foreground = new->clock.Hdpixel;
	  XtReleaseGC (gcurrent, current->clock.HandGC);
	  new->clock.HandGC = XtGetGC((Widget)gcurrent, valuemask, &myXGCV);
	  redisplay = TRUE;
          }

      if (new->core.background_pixel != current->core.background_pixel) {
          valuemask = GCForeground | GCLineWidth | GCGraphicsExposures;
	  myXGCV.foreground = new->core.background_pixel;
	  myXGCV.line_width = 0;
	  myXGCV.graphics_exposures = False;
	  XtReleaseGC (gcurrent, current->clock.EraseGC);
	  new->clock.EraseGC = XtGetGC((Widget)gcurrent, valuemask, &myXGCV);
	  redisplay = TRUE;
	  }
#ifdef XRENDER
     if (new->clock.face != current->clock.face)
	redisplay = TRUE;
     if (!sameColor (&new->clock.hour_color, &current->clock.fg_color) ||
	 !sameColor (&new->clock.hour_color, &current->clock.hour_color) ||
	 !sameColor (&new->clock.min_color, &current->clock.min_color) ||
	 !sameColor (&new->clock.sec_color, &current->clock.sec_color) ||
	 !sameColor (&new->clock.major_color, &current->clock.major_color) ||
	 !sameColor (&new->clock.minor_color, &current->clock.minor_color))
	redisplay = True;
    if (new->clock.sharp != current->clock.sharp)
	redisplay = True;
    if (new->clock.render != current->clock.render)
	redisplay = True;
    if (new->clock.buffer != current->clock.buffer)
    {
	if (new->clock.pixmap)
	{
	    XFreePixmap (XtDisplay (new), new->clock.pixmap);
	    new->clock.pixmap = 0;
	}
	if (new->clock.draw)
	{
	    XftDrawDestroy (new->clock.draw);
	    new->clock.draw = NULL;
	}
	new->clock.picture = 0;
    }
#endif
     return (redisplay);

}

#if !defined(NO_I18N) && defined(HAVE_ICONV)
static char *
clock_to_utf8(const char *str, int in_len)
{
    iconv_t cd;
    char *buf;
    size_t buf_size;
    size_t ileft, oleft;
    ICONV_CONST char *inptr;
    char *outptr;
    size_t ret;
    const char *code_set = nl_langinfo(CODESET);

    if (str == NULL ||code_set == NULL || strcasecmp(code_set, "646") == 0)
    	return NULL;

    if (strcasecmp(code_set, "UTF-8") == 0)
    	return strdup(str);

    cd = iconv_open("UTF-8", code_set);
    if (cd == (iconv_t)-1)
    	return NULL;

    buf_size = MB_LEN_MAX * (in_len + 1);
    if ((buf = malloc(buf_size)) == NULL) {
    	(void) iconv_close(cd);
    	return NULL;
    }

    inptr = str;
    ileft = in_len;
    outptr = buf;
    oleft = buf_size;

    ret = iconv(cd, &inptr, &ileft, &outptr, &oleft);
    if (ret == (size_t)(-1) || oleft == 0 ) {
        free(buf);
        buf = NULL;
    } else
	*outptr = '\0';

    (void) iconv_close(cd);
    return buf;
}
#endif
