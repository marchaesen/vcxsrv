/*
 *
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
 *
 * Author:  Jim Fulton, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <ctype.h>
#include <math.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xos.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/PannerP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#if defined(ISC) && __STDC__ && !defined(ISC30)
extern double atof(char *);
#else
#include <stdlib.h>			/* for atof() */
#endif

/*
 * Class Methods
 */
static void XawPannerDestroy(Widget);
static void XawPannerInitialize(Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult XawPannerQueryGeometry(Widget, XtWidgetGeometry*,
					       XtWidgetGeometry*);
static void XawPannerRealize(Widget, XtValueMask*, XSetWindowAttributes*);
static void XawPannerRedisplay(Widget, XEvent*, Region);
static void XawPannerResize(Widget);
static Boolean XawPannerSetValues(Widget, Widget, Widget, ArgList, Cardinal*);
static void XawPannerSetValuesAlmost(Widget, Widget, XtWidgetGeometry*,
				     XtWidgetGeometry*);

/*
 * Prototypes
 */
static void check_knob(PannerWidget, Bool);
static void get_default_size(PannerWidget, Dimension*, Dimension*);
static Bool get_event_xy(PannerWidget, XEvent*, int*, int*);
static void move_shadow(PannerWidget);
static int parse_page_string(String, int, int, Bool*);
static void rescale(PannerWidget);
static void reset_shadow_gc(PannerWidget);
static void reset_slider_gc(PannerWidget);
static void reset_xor_gc(PannerWidget);
static void scale_knob(PannerWidget, Bool, Bool);

/*
 * Actions
 */
static void ActionAbort(Widget, XEvent*, String*, Cardinal*);
static void ActionMove(Widget, XEvent*, String*, Cardinal*);
static void ActionNotify(Widget, XEvent*, String*, Cardinal*);
static void ActionPage(Widget, XEvent*, String*, Cardinal*);
static void ActionSet(Widget, XEvent*, String*, Cardinal*);
static void ActionStart(Widget, XEvent*, String*, Cardinal*);
static void ActionStop(Widget, XEvent*, String*, Cardinal*);

/*
 * From Xmu/Distinct.c
 */
Bool XmuDistinguishablePixels(Display*, Colormap, unsigned long*, int);

/*
 * Initialization
 */
static char defaultTranslations[] =
"<Btn1Down>:"		"start()\n"
"<Btn1Motion>:"		"move()\n"
"<Btn1Up>:"		"notify() stop()\n"
"<Btn2Down>:"		"abort()\n"
":<Key>KP_Enter:"	"set(rubberband,toggle)\n"
"<Key>space:"		"page(+1p,+1p)\n"
"<Key>Delete:"		"page(-1p,-1p)\n"
":<Key>KP_Delete:"	"page(-1p,-1p)\n"
"<Key>BackSpace:"	"page(-1p,-1p)\n"
"<Key>Left:"		"page(-.5p,+0)\n"
":<Key>KP_Left:"	"page(-.5p,+0)\n"
"<Key>Right:"		"page(+.5p,+0)\n"
":<Key>KP_Right:"	"page(+.5p,+0)\n"
"<Key>Up:"		"page(+0,-.5p)\n"
":<Key>KP_Up:"		"page(+0,-.5p)\n"
"<Key>Down:"		"page(+0,+.5p)\n"
":<Key>KP_Down:"	"page(+0,+.5p)\n"
"<Key>Home:"		"page(0,0)\n"
":<Key>KP_Home:"	"page(0,0)\n"
;

static XtActionsRec actions[] = {
  {"start",	ActionStart},		/* start tmp graphics */
  {"stop",	ActionStop},		/* stop tmp graphics */
  {"abort",	ActionAbort},		/* punt */
  {"move",	ActionMove},		/* move tmp graphics on Motion event */
  {"page",	ActionPage},		/* page around usually from keyboard */
  {"notify",	ActionNotify},		/* callback new position */
  {"set",	ActionSet},		/* set various parameters */
};

#define offset(field)	XtOffsetOf(PannerRec, panner.field)
static XtResource resources[] = {
    {
      XtNallowOff,
      XtCAllowOff,
      XtRBoolean,
      sizeof(Boolean),
      offset(allow_off),
      XtRImmediate,
      (XtPointer)False
    },
    {
      XtNresize,
      XtCResize,
      XtRBoolean,
      sizeof(Boolean),
      offset(resize_to_pref),
      XtRImmediate,
      (XtPointer)True
    },
    {
      XtNreportCallback,
      XtCReportCallback,
      XtRCallback,
      sizeof(XtPointer),
      offset(report_callbacks),
      XtRCallback,
      NULL
    },
    {
      XtNdefaultScale,
      XtCDefaultScale,
      XtRDimension,
      sizeof(Dimension),
      offset(default_scale),
      XtRImmediate,
      (XtPointer)PANNER_DEFAULT_SCALE
    },
    {
      XtNrubberBand,
      XtCRubberBand,
      XtRBoolean,
      sizeof(Boolean),
      offset(rubber_band),
      XtRImmediate,
      (XtPointer)False
    },
    {
      XtNforeground,
      XtCForeground,
      XtRPixel,
      sizeof(Pixel),
      offset(foreground),
      XtRString,
      (XtPointer)XtDefaultBackground
    },
    {
      XtNinternalSpace,
      XtCInternalSpace,
      XtRDimension,
      sizeof(Dimension),
      offset(internal_border),
      XtRImmediate,
      (XtPointer)4
    },
    {
      XtNlineWidth,
      XtCLineWidth,
      XtRDimension,
      sizeof(Dimension),
      offset(line_width),
      XtRImmediate,
      (XtPointer)0
    },
    {
      XtNcanvasWidth,
      XtCCanvasWidth,
      XtRDimension,
      sizeof(Dimension),
      offset(canvas_width),
      XtRImmediate,
      (XtPointer)0
    },
    {
      XtNcanvasHeight,
      XtCCanvasHeight,
      XtRDimension,
      sizeof(Dimension),
      offset(canvas_height),
      XtRImmediate,
      (XtPointer)0
    },
    {
      XtNsliderX,
      XtCSliderX,
      XtRPosition,
      sizeof(Position),
      offset(slider_x),
      XtRImmediate,
      (XtPointer)0
    },
    {
      XtNsliderY,
      XtCSliderY,
      XtRPosition,
      sizeof(Position),
      offset(slider_y),
      XtRImmediate,
      (XtPointer)0
    },
    {
      XtNsliderWidth,
      XtCSliderWidth,
      XtRDimension,
      sizeof(Dimension),
      offset(slider_width),
      XtRImmediate,
      (XtPointer)0
    },
    {
      XtNsliderHeight,
      XtCSliderHeight,
      XtRDimension,
      sizeof(Dimension),
      offset(slider_height),
      XtRImmediate,
      (XtPointer)0
    },
    {
      XtNshadowColor,
      XtCShadowColor,
      XtRPixel,
      sizeof(Pixel),
      offset(shadow_color),
      XtRString,
      (XtPointer)XtDefaultForeground
    },
    {
      XtNshadowThickness,
      XtCShadowThickness,
      XtRDimension,
      sizeof(Dimension),
      offset(shadow_thickness),
      XtRImmediate,
      (XtPointer)2
    },
    {
      XtNbackgroundStipple,
      XtCBackgroundStipple,
      XtRString,
      sizeof(String),
      offset(stipple_name),
      XtRImmediate,
      NULL
    },
};
#undef offset

#define Superclass	(&simpleClassRec)
PannerClassRec pannerClassRec = {
  /* core */
  {
    (WidgetClass)Superclass,		/* superclass */
    "Panner",				/* class_name */
    sizeof(PannerRec),			/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawPannerInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    XawPannerRealize,			/* realize */
    actions,				/* actions */
    XtNumber(actions),			/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawPannerDestroy,			/* destroy */
    XawPannerResize,			/* resize */
    XawPannerRedisplay,			/* expose */
    XawPannerSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XawPannerSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    defaultTranslations,		/* tm_table */
    XawPannerQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* simple */
  {
    XtInheritChangeSensitive,		/* change_sensitive */
#ifndef OLDXAW
    NULL,
#endif
  },
  /* panner */
  {
    NULL,				/* extension */
  }
};

WidgetClass pannerWidgetClass = (WidgetClass) &pannerClassRec;


/*
 * Implementation
 */
static void
reset_shadow_gc(PannerWidget pw)
{
    XtGCMask valuemask = GCForeground;
    XGCValues values;
    unsigned long   pixels[3];

    if (pw->panner.shadow_gc)
	XtReleaseGC((Widget)pw, pw->panner.shadow_gc);

    pixels[0] = pw->panner.foreground;
    pixels[1] = pw->core.background_pixel;
    pixels[2] = pw->panner.shadow_color;

    if (!pw->panner.stipple_name &&
	!XmuDistinguishablePixels(XtDisplay(pw), pw->core.colormap,
				  pixels, 3) &&
	XmuDistinguishablePixels(XtDisplay(pw), pw->core.colormap,
				 pixels, 2)) {
	valuemask = GCTile | GCFillStyle;
	values.fill_style = FillTiled;
	values.tile = XmuCreateStippledPixmap(XtScreen((Widget)pw),
					      pw->panner.foreground,
					      pw->core.background_pixel,
					      pw->core.depth);
    }
    else {
	if (!pw->panner.line_width &&
	    !XmuDistinguishablePixels(XtDisplay(pw), pw->core.colormap,
				      pixels, 2))
	    pw->panner.line_width = 1;
	valuemask = GCForeground;
	values.foreground = pw->panner.shadow_color;
    }
    if (pw->panner.line_width > 0) {
	values.line_width = pw->panner.line_width;
	valuemask |= GCLineWidth;
    }

    pw->panner.shadow_gc = XtGetGC((Widget)pw, valuemask, &values);
}

static void
reset_slider_gc(PannerWidget pw)
{
    XtGCMask valuemask = GCForeground;
    XGCValues values;

    if (pw->panner.slider_gc)
	XtReleaseGC((Widget)pw, pw->panner.slider_gc);

    values.foreground = pw->panner.foreground;

    pw->panner.slider_gc = XtGetGC((Widget)pw, valuemask, &values);
}

static void
reset_xor_gc(PannerWidget pw)
{
    if (pw->panner.xor_gc)
	XtReleaseGC((Widget)pw, pw->panner.xor_gc);

    if (pw->panner.rubber_band) {
	XtGCMask valuemask = (GCForeground | GCFunction);
	XGCValues values;
	Pixel tmp;

	tmp = (pw->panner.foreground == pw->core.background_pixel ?
	       pw->panner.shadow_color : pw->panner.foreground);
	values.foreground = tmp ^ pw->core.background_pixel;
	values.function = GXxor;
	if (pw->panner.line_width > 0) {
	    valuemask |= GCLineWidth;
	    values.line_width = pw->panner.line_width;
	}
	pw->panner.xor_gc = XtGetGC((Widget)pw, valuemask, &values);
    }
    else
	pw->panner.xor_gc = NULL;
}

static void
check_knob(PannerWidget pw, Bool knob)
{
    Position pad = (Position)(pw->panner.internal_border << 1);
    Position maxx = (Position)(XtWidth(pw) - pad - pw->panner.knob_width);
    Position maxy = (Position)(XtHeight(pw) - pad - pw->panner.knob_height);
    Position *x = knob ? &pw->panner.knob_x : &pw->panner.tmp.x;
    Position *y = knob ? &pw->panner.knob_y : &pw->panner.tmp.y;

    /*
     * note that positions are already normalized (i.e. internal_border
     * has been subtracted out)
     */
    if (*x < 0)
	*x = 0;
    if (*x > maxx)
	*x = maxx;

    if (*y < 0)
	*y = 0;
    if (*y > maxy)
	*y = maxy;

    if (knob) {
	pw->panner.slider_x = (Position)((double)pw->panner.knob_x
					/ pw->panner.haspect + 0.5);
	pw->panner.slider_y = (Position)((double)pw->panner.knob_y
					/ pw->panner.vaspect + 0.5);
	pw->panner.last_x = pw->panner.last_y = PANNER_OUTOFRANGE;
    }
}

static void
move_shadow(PannerWidget pw)
{
    if (pw->panner.shadow_thickness > 0) {
	int lw = pw->panner.shadow_thickness + (pw->panner.line_width << 1);
	int pad = pw->panner.internal_border;

	if (pw->panner.knob_height > lw && pw->panner.knob_width > lw) {
	    XRectangle *r = pw->panner.shadow_rects;

	    r->x = (short)(pw->panner.knob_x + pad + pw->panner.knob_width);
	    r->y = (short)(pw->panner.knob_y + pad + lw);
	    r->width = (unsigned short)(pw->panner.shadow_thickness);
	    r->height = (unsigned short)(pw->panner.knob_height - lw);
	    r++;
	    r->x = (short)(pw->panner.knob_x + pad + lw);
	    r->y = (short)(pw->panner.knob_y + pad + pw->panner.knob_height);
	    r->width = (unsigned short)(pw->panner.knob_width - lw + pw->panner.shadow_thickness);
	    r->height = (unsigned short)(pw->panner.shadow_thickness);
	    pw->panner.shadow_valid = True;
	    return;
	}
    }
    pw->panner.shadow_valid = False;
}

static void
scale_knob(PannerWidget pw, Bool location, Bool size)
{
    if (location) {
	pw->panner.knob_x = (Position)PANNER_HSCALE(pw, pw->panner.slider_x);
	pw->panner.knob_y = (Position)PANNER_VSCALE(pw, pw->panner.slider_y);
    }
    if (size) {
	Dimension width, height;

	if (pw->panner.slider_width < 1)
	    pw->panner.slider_width = pw->panner.canvas_width;
	if (pw->panner.slider_height < 1)
	    pw->panner.slider_height = pw->panner.canvas_height;
	width = Min(pw->panner.slider_width, pw->panner.canvas_width);
	height = Min(pw->panner.slider_height, pw->panner.canvas_height);

	pw->panner.knob_width = (Dimension)PANNER_HSCALE(pw, width);
	pw->panner.knob_height = (Dimension)PANNER_VSCALE(pw, height);
    }
    if (!pw->panner.allow_off)
	check_knob(pw, True);
    move_shadow(pw);
}

static void
rescale(PannerWidget pw)
{
    int hpad = pw->panner.internal_border << 1;
    int vpad = hpad;

    if (pw->panner.canvas_width < 1)
	pw->panner.canvas_width = XtWidth(pw);
    if (pw->panner.canvas_height < 1)
	pw->panner.canvas_height = XtHeight(pw);

    if (XtWidth(pw) <= hpad)
	hpad = 0;
    if (XtHeight(pw) <= vpad)
	vpad = 0;

    pw->panner.haspect = ((double)XtWidth(pw) - hpad + .5)
			 / (double)pw->panner.canvas_width;
    pw->panner.vaspect = ((double)XtHeight(pw) - vpad + .5)
			 / (double)pw->panner.canvas_height;
    scale_knob(pw, True, True);
}

static void
get_default_size(PannerWidget pw, Dimension *wp, Dimension *hp)
{
    Dimension pad = (Dimension)(pw->panner.internal_border << 1);

    *wp = (Dimension)(PANNER_DSCALE(pw, pw->panner.canvas_width) + pad);
    *hp = (Dimension)(PANNER_DSCALE(pw, pw->panner.canvas_height) + pad);
}

static Bool
get_event_xy(PannerWidget pw, XEvent *event, int *x, int *y)
{
    int pad = pw->panner.internal_border;

    switch (event->type) {
	case ButtonPress:
	case ButtonRelease:
	    *x = event->xbutton.x - pad;
	    *y = event->xbutton.y - pad;
	    return (True);
	case KeyPress:
	case KeyRelease:
	    *x = event->xkey.x - pad;
	    *y = event->xkey.y - pad;
	    return (True);
	case EnterNotify:
	case LeaveNotify:
	    *x = event->xcrossing.x - pad;
	    *y = event->xcrossing.y - pad;
	    return (True);
	case MotionNotify:
	    *x = event->xmotion.x - pad;
	    *y = event->xmotion.y - pad;
	    return (True);
    }

    return (False);
}

static int
parse_page_string(String s, int pagesize, int canvassize, Bool *relative)
{
    String cp;
    double val = 1.0;
    Bool rel = False;

    /*
     * syntax:    spaces [+-] number spaces [pc\0] spaces
     */
    for (; isascii((unsigned char)*s) && isspace((unsigned char)*s); s++)	/* skip white space */
	;

    if (*s == '+' || *s == '-')	{		/* deal with signs */
	rel = True;
	if (*s == '-')
	    val = -1.0;
	s++;
    }
    if (!*s) {				/* if null then return nothing */
	*relative = True;
	return (0);
    }

					/* skip over numbers */
    for (cp = s; isascii((unsigned char)*s) && (isdigit((unsigned char)*s) || *s == '.'); s++)
	;
    val *= atof(cp);

					/* skip blanks */
    for (; isascii((unsigned char)*s) && isspace((unsigned char)*s); s++)
	;

    if (*s) {				/* if units */
	switch (s[0]) {
	    case 'p':
	    case 'P':
		val *= (double)pagesize;
		break;
	    case 'c':
	    case 'C':
		val *= (double)canvassize;
		break;
	}
    }
    *relative = rel;

    return ((int)val);
}

#define DRAW_TMP(pw) \
{ \
    XDrawRectangle(XtDisplay(pw), XtWindow(pw),				\
		   pw->panner.xor_gc,					\
		   (pw->panner.tmp.x + pw->panner.internal_border),	\
		   (pw->panner.tmp.y + pw->panner.internal_border),	\
		   (unsigned)(pw->panner.knob_width - 1),		\
		   (unsigned)(pw->panner.knob_height - 1));		\
    pw->panner.tmp.showing = !pw->panner.tmp.showing;			\
}

#define UNDRAW_TMP(pw) \
{ \
    if (pw->panner.tmp.showing)			\
      DRAW_TMP(pw);				\
}

#define BACKGROUND_STIPPLE(pw) \
XmuLocatePixmapFile(pw->core.screen, pw->panner.stipple_name,		\
		    pw->panner.shadow_color, pw->core.background_pixel,	\
		    pw->core.depth, NULL, 0, NULL, NULL, NULL, NULL)

#define PIXMAP_OKAY(pm) ((pm) != None && (pm) != XtUnspecifiedPixmap)

/*ARGSUSED*/
static void
XawPannerInitialize(Widget greq, Widget gnew, ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    PannerWidget req = (PannerWidget)greq, cnew = (PannerWidget)gnew;
    Dimension defwidth, defheight;

    if (req->panner.canvas_width < 1)
	cnew->panner.canvas_width = 1;
    if (req->panner.canvas_height < 1)
	cnew->panner.canvas_height = 1;
    if (req->panner.default_scale < 1)
	cnew->panner.default_scale = PANNER_DEFAULT_SCALE;

    get_default_size(req, &defwidth, &defheight);
    if (XtWidth(req) < 1)
	XtWidth(cnew) = defwidth;
    if (XtHeight(req) < 1)
	XtHeight(cnew) = defheight;

    cnew->panner.shadow_gc = NULL;
    reset_shadow_gc(cnew);		/* shadowColor */
    cnew->panner.slider_gc = NULL;
    reset_slider_gc(cnew);		/* foreground */
    cnew->panner.xor_gc = NULL;
    reset_xor_gc(cnew);			/* foreground ^ background */

    rescale(cnew);			/* does a position check */
    cnew->panner.shadow_valid = False;
    cnew->panner.tmp.doing = False;
    cnew->panner.tmp.showing = False;
  }

static void
XawPannerRealize(Widget gw, XtValueMask *valuemaskp,
		 XSetWindowAttributes *attr)
{
    PannerWidget pw = (PannerWidget)gw;
    Pixmap pm = XtUnspecifiedPixmap;
    Bool gotpm = False;

    if (pw->core.background_pixmap == XtUnspecifiedPixmap) {
	if (pw->panner.stipple_name)
	    pm = BACKGROUND_STIPPLE(pw);

	if (PIXMAP_OKAY(pm)) {
	    attr->background_pixmap = pm;
	    *valuemaskp |= CWBackPixmap;
	    *valuemaskp &= (XtValueMask)(~CWBackPixel);
	    gotpm = True;
	}
    }
    (*pannerWidgetClass->core_class.superclass->core_class.realize)
	(gw, valuemaskp, attr);

    if (gotpm)
	XFreePixmap(XtDisplay(gw), pm);
}

static void
XawPannerDestroy(Widget gw)
{
    PannerWidget pw = (PannerWidget)gw;

    XtReleaseGC(gw, pw->panner.shadow_gc);
    XtReleaseGC(gw, pw->panner.slider_gc);
    XtReleaseGC(gw, pw->panner.xor_gc);
}

static void
XawPannerResize(Widget gw)
{
    rescale((PannerWidget)gw);
}

static void
XawPannerRedisplay(Widget gw, XEvent *event, Region region)
{
    PannerWidget pw = (PannerWidget)gw;
    Display *dpy = XtDisplay(gw);
    Window w = XtWindow(gw);
    int pad = pw->panner.internal_border;
    Dimension lw = pw->panner.line_width;
    Dimension extra = (Dimension)(pw->panner.shadow_thickness + (lw << 1));
    int kx = pw->panner.knob_x + pad, ky = pw->panner.knob_y + pad;

    if (Superclass->core_class.expose)
	(Superclass->core_class.expose)(gw, event, region);

    pw->panner.tmp.showing = False;
    XClearArea(XtDisplay(pw), XtWindow(pw),
	       (int)pw->panner.last_x - ((int)lw) + pad,
	       (int)pw->panner.last_y - ((int)lw) + pad,
	       (unsigned)(pw->panner.knob_width + extra),
	       (unsigned)(pw->panner.knob_height + extra),
	       False);
    pw->panner.last_x = pw->panner.knob_x;
    pw->panner.last_y = pw->panner.knob_y;

    XFillRectangle(dpy, w, pw->panner.slider_gc, kx, ky,
		   (unsigned)(pw->panner.knob_width - 1),
		   (unsigned)(pw->panner.knob_height - 1));

    if (lw)
	XDrawRectangle(dpy, w, pw->panner.shadow_gc, kx, ky,
		       (unsigned)(pw->panner.knob_width - 1),
		       (unsigned)(pw->panner.knob_height - 1));

    if (pw->panner.shadow_valid)
	XFillRectangles(dpy, w, pw->panner.shadow_gc, pw->panner.shadow_rects, 2);

    if (pw->panner.tmp.doing && pw->panner.rubber_band)
	DRAW_TMP(pw);
}

/*ARGSUSED*/
static Boolean
XawPannerSetValues(Widget gcur, Widget greq _X_UNUSED, Widget gnew,
		   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    PannerWidget cur = (PannerWidget)gcur;
    PannerWidget cnew = (PannerWidget)gnew;
    Bool redisplay = False;

    if (cur->panner.foreground != cnew->panner.foreground) {
	reset_slider_gc(cnew);
	if (cur->panner.foreground != cur->core.background_pixel)
	    reset_xor_gc(cnew);
	redisplay = True;
    }
    else if (cur->panner.line_width != cnew->panner.line_width ||
	     cur->core.background_pixel != cnew->core.background_pixel) {
	reset_xor_gc(cnew);
	redisplay = True;
    }
    if (cur->panner.shadow_color != cnew->panner.shadow_color) {
	reset_shadow_gc(cnew);
	if (cur->panner.foreground == cur->core.background_pixel)
	    reset_xor_gc(cnew);
	redisplay = True;
    }
    if (cur->panner.shadow_thickness != cnew->panner.shadow_thickness) {
	move_shadow(cnew);
	redisplay = True;
    }
    if (cur->panner.rubber_band != cnew->panner.rubber_band) {
	reset_xor_gc(cnew);
	if (cnew->panner.tmp.doing)
	    redisplay = True;
    }

    if ((cur->panner.stipple_name != cnew->panner.stipple_name
	 || cur->panner.shadow_color != cnew->panner.shadow_color
	 || cur->core.background_pixel != cnew->core.background_pixel)
	&& XtIsRealized(gnew)) {
	Pixmap pm = cnew->panner.stipple_name ?
			BACKGROUND_STIPPLE(cnew) : XtUnspecifiedPixmap;

	if (PIXMAP_OKAY(pm)) {
	    XSetWindowBackgroundPixmap(XtDisplay(cnew), XtWindow(cnew), pm);
	    XFreePixmap(XtDisplay(cnew), pm);
	}
	else
	    XSetWindowBackground(XtDisplay(cnew), XtWindow(cnew),
				 cnew->core.background_pixel);

	redisplay = True;
    }

    if (cnew->panner.resize_to_pref &&
	(cur->panner.canvas_width != cnew->panner.canvas_width
	 || cur->panner.canvas_height != cnew->panner.canvas_height
	 || cur->panner.resize_to_pref != cnew->panner.resize_to_pref)) {
	get_default_size(cnew, &cnew->core.width, &cnew->core.height);
	redisplay = True;
    }
    else if (cur->panner.canvas_width != cnew->panner.canvas_width
	     || cur->panner.canvas_height != cnew->panner.canvas_height
	     || cur->panner.internal_border != cnew->panner.internal_border) {
	rescale(cnew);			/* does a scale_knob as well */
	redisplay = True;
    }
    else {
	Bool loc = cur->panner.slider_x != cnew->panner.slider_x ||
		   cur->panner.slider_y != cnew->panner.slider_y;
	Bool siz = cur->panner.slider_width != cnew->panner.slider_width ||
		   cur->panner.slider_height != cnew->panner.slider_height;
	if (loc || siz || (cur->panner.allow_off != cnew->panner.allow_off
			   && cnew->panner.allow_off)) {
	    scale_knob(cnew, loc, siz);
	    redisplay = True;
	}
    }

    return (Boolean)(redisplay);
}

static void
XawPannerSetValuesAlmost(Widget gold, Widget gnew, XtWidgetGeometry *req,
			 XtWidgetGeometry *reply)
{
    if (reply->request_mode == 0)	/* got turned down, so cope */
	XawPannerResize(gnew);

    (*pannerWidgetClass->core_class.superclass->core_class.set_values_almost)
	(gold, gnew, req, reply);
}

static XtGeometryResult
XawPannerQueryGeometry(Widget gw, XtWidgetGeometry *intended,
		       XtWidgetGeometry *pref)
{
    PannerWidget pw = (PannerWidget)gw;

    pref->request_mode = (CWWidth | CWHeight);
    get_default_size(pw, &pref->width, &pref->height);

    if (((intended->request_mode & (CWWidth | CWHeight)) == (CWWidth | CWHeight))
	&& intended->width == pref->width && intended->height == pref->height)
	return (XtGeometryYes);
    else if (pref->width == XtWidth(pw) && pref->height == XtHeight(pw))
	return (XtGeometryNo);

    return (XtGeometryAlmost);
}


/*ARGSUSED*/
static void
ActionStart(Widget gw, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    PannerWidget pw = (PannerWidget)gw;
    int x, y;

    if (!get_event_xy(pw, event, &x, &y)) {
	XBell(XtDisplay(gw), 0);
	return;
    }

    pw->panner.tmp.doing = True;
    pw->panner.tmp.startx = pw->panner.knob_x;
    pw->panner.tmp.starty = pw->panner.knob_y;
    pw->panner.tmp.dx = (Position)(x - pw->panner.knob_x);
    pw->panner.tmp.dy = (Position)(y - pw->panner.knob_y);
    pw->panner.tmp.x = pw->panner.knob_x;
    pw->panner.tmp.y = pw->panner.knob_y;
    if (pw->panner.rubber_band)
	DRAW_TMP(pw);
}

/*ARGSUSED*/
static void
ActionStop(Widget gw, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    PannerWidget pw = (PannerWidget)gw;
    int x, y;

    if (get_event_xy(pw, event, &x, &y)) {
	pw->panner.tmp.x = (Position)(x - pw->panner.tmp.dx);
	pw->panner.tmp.y = (Position)(y - pw->panner.tmp.dy);
	if (!pw->panner.allow_off)
	    check_knob(pw, False);
    }
    if (pw->panner.rubber_band)
	DRAW_TMP(pw);
    pw->panner.tmp.doing = False;
}

static void
ActionAbort(Widget gw, XEvent *event, String *params, Cardinal *num_params)
{
    PannerWidget pw = (PannerWidget)gw;

    if (!pw->panner.tmp.doing)
	return;

    if (pw->panner.rubber_band)
	UNDRAW_TMP(pw);

    if (!pw->panner.rubber_band) {		/* restore old position */
	pw->panner.tmp.x = pw->panner.tmp.startx;
	pw->panner.tmp.y = pw->panner.tmp.starty;
	ActionNotify(gw, event, params, num_params);
    }
    pw->panner.tmp.doing = False;
}

static void
ActionMove(Widget gw, XEvent *event, String *params, Cardinal *num_params)
{
    PannerWidget pw = (PannerWidget)gw;
    int x, y;

    if (!pw->panner.tmp.doing)
      return;

    if (!get_event_xy(pw, event, &x, &y)) {
	XBell(XtDisplay(gw), 0);	/* should do error message */
	return;
    }

    if (pw->panner.rubber_band)
	UNDRAW_TMP(pw);
    pw->panner.tmp.x = (Position)(x - pw->panner.tmp.dx);
    pw->panner.tmp.y = (Position)(y - pw->panner.tmp.dy);

    if (!pw->panner.rubber_band)
	ActionNotify(gw, event, params, num_params);
    else {
	if (!pw->panner.allow_off)
	    check_knob(pw, False);
	DRAW_TMP(pw);
    }
}


static void
ActionPage(Widget gw, XEvent *event, String *params, Cardinal *num_params)
{
    PannerWidget pw = (PannerWidget)gw;
    Cardinal zero = 0;
    Bool isin = pw->panner.tmp.doing;
    int x, y;
    Bool relx, rely;
    int pad = pw->panner.internal_border << 1;

    if (*num_params != 2) {
      XBell(XtDisplay(gw), 0);
	return;
    }

    x = parse_page_string(params[0], pw->panner.knob_width,
			  (int)XtWidth(pw) - pad, &relx);
    y = parse_page_string(params[1], pw->panner.knob_height,
			  (int)XtHeight(pw) - pad, &rely);

    if (relx)
	x += pw->panner.knob_x;
    if (rely)
	y += pw->panner.knob_y;

    if (isin) {				/* if in, then use move */
	XEvent ev;

	ev.xbutton.type = ButtonPress;
	ev.xbutton.x = x;
	ev.xbutton.y = y;
	ActionMove(gw, &ev, NULL, &zero);
    }
    else {
	pw->panner.tmp.doing = True;
	pw->panner.tmp.x = (Position)x;
	pw->panner.tmp.y = (Position)y;
	ActionNotify(gw, event, NULL, &zero);
	pw->panner.tmp.doing = False;
    }
}

/*ARGSUSED*/
static void
ActionNotify(Widget gw, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    PannerWidget pw = (PannerWidget)gw;

    if (!pw->panner.tmp.doing)
	return;

    if (!pw->panner.allow_off)
	check_knob(pw, False);
    pw->panner.knob_x = pw->panner.tmp.x;
    pw->panner.knob_y = pw->panner.tmp.y;
    move_shadow(pw);

    pw->panner.slider_x = (Position)((double)pw->panner.knob_x
				   / pw->panner.haspect + 0.5);
    pw->panner.slider_y = (Position)((double) pw->panner.knob_y
				   / pw->panner.vaspect + 0.5);
    if (!pw->panner.allow_off) {
	Position tmp;

	if (pw->panner.slider_x
	    > (tmp = (Position)(pw->panner.canvas_width -
				pw->panner.slider_width)))
	    pw->panner.slider_x = tmp;
	if (pw->panner.slider_x < 0)
	    pw->panner.slider_x = 0;
	if (pw->panner.slider_y
	    > (tmp = (Position)(pw->panner.canvas_height -
				pw->panner.slider_height)))
	    pw->panner.slider_y = tmp;
	if (pw->panner.slider_y < 0)
	    pw->panner.slider_y = 0;
    }

    if (pw->panner.last_x != pw->panner.knob_x ||
	pw->panner.last_y != pw->panner.knob_y) {
	XawPannerReport rep;

	XawPannerRedisplay(gw, NULL, NULL);
	rep.changed = XawPRSliderX | XawPRSliderY;
	rep.slider_x = pw->panner.slider_x;
	rep.slider_y = pw->panner.slider_y;
	rep.slider_width = pw->panner.slider_width;
	rep.slider_height = pw->panner.slider_height;
	rep.canvas_width = pw->panner.canvas_width;
	rep.canvas_height = pw->panner.canvas_height;
	XtCallCallbackList(gw, pw->panner.report_callbacks, (XtPointer)&rep);
    }
}

/*ARGSUSED*/
static void
ActionSet(Widget gw, XEvent *event _X_UNUSED, String *params, Cardinal *num_params)
{
    PannerWidget pw = (PannerWidget)gw;
    Bool rb;

    if (*num_params < 2 ||
	XmuCompareISOLatin1(params[0], "rubberband") != 0) {
	XBell(XtDisplay(gw), 0);
	return;
    }

    if (XmuCompareISOLatin1(params[1], "on") == 0)
	rb = True;
    else if (XmuCompareISOLatin1(params[1], "off") == 0)
	rb = False;
    else if (XmuCompareISOLatin1(params[1], "toggle") == 0)
	rb = !pw->panner.rubber_band;
    else {
      XBell(XtDisplay(gw), 0);
	return;
    }

    if (rb != pw->panner.rubber_band) {
	Arg args[1];

	XtSetArg(args[0], XtNrubberBand, rb);
	XtSetValues(gw, args, 1);
    }
}
