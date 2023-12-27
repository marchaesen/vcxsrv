/*

Copyright 1988, 1998  The Open Group

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/extensions/shape.h>
#include "Converters.h"
#include "Drawing.h"
#include "Misc.h"

/*
 * Prototypes
 */
static void ShapeEllipseOrRoundedRectangle(Widget, Bool, int, int);
static void ShapeError(Widget);
static void ShapeOval(Widget);
static void ShapeRectangle(Widget);

/*
 * Implementation
 */
Boolean
XmuReshapeWidget(Widget w, int shape_style,
		 int corner_width, int corner_height)
{
  switch (shape_style)
    {
      case XmuShapeRectangle:
	ShapeRectangle(w);
	break;
      case XmuShapeOval:
	ShapeOval(w);
	break;
      case XmuShapeEllipse:
      case XmuShapeRoundedRectangle:
      ShapeEllipseOrRoundedRectangle(w, shape_style == XmuShapeEllipse,
				     corner_width, corner_height);
	break;
      default:
	ShapeError(w);
      return (False);
    }
  return (True);
}

static void
ShapeError(Widget w)
{
    String params[1];
    Cardinal num_params = 1;

    params[0] = XtName(w);
  XtAppWarningMsg(XtWidgetToApplicationContext(w),
		     "shapeUnknown", "xmuReshapeWidget", "XmuLibrary",
		     "Unsupported shape style for Command widget \"%s\"",
		  params, &num_params);
}

static void
ShapeRectangle(Widget w)
{
  XShapeCombineMask(XtDisplay(w), XtWindow(w),
		    ShapeBounding, 0, 0, None, ShapeSet);
  XShapeCombineMask(XtDisplay(w), XtWindow(w),
		    ShapeClip, 0, 0, None, ShapeSet);
}

/*
 * Function:
 *	ShapeOval
 *
 * Parameters:
 *	w - widget to be reshaped
 *
 * Description:
 *	Reshapes a widget to a oval format.
 *
 * Notes:
 *	  X11R6.3 behaviour changed. Now if the height is larger than the
 *	width, this function inverts the sense of the oval, instead of
 *	fallbacking to ellipse.
 */
static void
ShapeOval(Widget w)
{
    Display *dpy = XtDisplay(w);
    int width = w->core.width;
    int height = w->core.height;
    Pixmap p;
    XGCValues values;
    GC gc;
    int rad;

    if (width < 3 || height < 3)
      return;
    width += w->core.border_width << 1;
    height += w->core.border_width << 1;

    p = XCreatePixmap(dpy, XtWindow(w), width, height, 1);
    values.foreground = 0;
    values.background = 1;
    values.cap_style = CapRound;
    values.line_width = Min(width, height);
    gc = XCreateGC(dpy, p,
		    GCForeground | GCBackground | GCLineWidth | GCCapStyle,
		    &values);
    XFillRectangle(dpy, p, gc, 0, 0, width, height);
    XSetForeground(dpy, gc, 1);

    if (width < height)
      {
	rad = width >> 1;
	XDrawLine(dpy, p, gc, rad, rad, rad, height - rad - 1);
      }
    else
      {
	rad = height >> 1;
	XDrawLine(dpy, p, gc, rad, rad, width - rad - 1, rad);
    }
    XShapeCombineMask(dpy, XtWindow(w), ShapeBounding,
		      -(int)w->core.border_width, -(int)w->core.border_width,
		      p, ShapeSet);
    if (w->core.border_width)
      {
	XSetForeground(dpy, gc, 0);
	XFillRectangle(dpy, p, gc, 0, 0, width, height);
	values.line_width = Min(w->core.width, w->core.height);
	values.foreground = 1;
	XChangeGC(dpy, gc, GCLineWidth | GCForeground, &values);
	if (w->core.width < w->core.height)
	  {
	    rad = w->core.width >> 1;
	    XDrawLine(dpy, p, gc, rad, rad, rad, w->core.height - rad - 1);
	  }
	else
	  {
	    rad = w->core.height >> 1;
	    XDrawLine(dpy, p, gc, rad, rad, w->core.width - rad - 1, rad);
	}
	XShapeCombineMask(dpy, XtWindow(w), ShapeClip, 0, 0, p, ShapeSet);
    }
    else
      XShapeCombineMask(XtDisplay(w), XtWindow(w),
			ShapeClip, 0, 0, None, ShapeSet);

    XFreePixmap(dpy, p);
    XFreeGC(dpy, gc);
}

/*
 * Function:
 *	ShapeEllipseOrRoundedRectangle
 *
 * Parameters:
 *	w	- widget to be reshaped
 *	ellipse - True if shape to ellipse, rounded rectangle otherwise
 *	ew	- horizontal radius of rounded rectangle
 *	eh	- vertical radius of rounded rectangle
 *
 * Description:
 *	  Based on the ellipse parameter, gives the widget a elliptical
 *	shape, or rounded rectangle shape.
 *
 * Notes:
 *	  The GC is created with a line width of 2, what seens to draw the
 *	widget border correctly, if the width - height is not proportional.
 */
static void
ShapeEllipseOrRoundedRectangle(Widget w, Bool ellipse, int ew, int eh)
{
    Display *dpy = XtDisplay(w);
  unsigned width = w->core.width;
  unsigned height = w->core.height;
  Pixmap p;
    XGCValues values;
    GC gc;
  unsigned long mask;

  if (width < 3 || height < 3)
    return;
  width += w->core.border_width << 1;
  height += w->core.border_width << 1;

  mask = GCForeground | GCLineWidth;
  p = XCreatePixmap(dpy, XtWindow(w), width, height, 1);

    values.foreground = 0;
  values.line_width = 2;

  gc = XCreateGC(dpy, p, mask, &values);
  XFillRectangle(dpy, p, gc, 0, 0, width, height);
  XSetForeground(dpy, gc, 1);
    if (!ellipse)
    XmuFillRoundedRectangle(dpy, p, gc, 1, 1, width - 2, height - 2, ew, eh);
    else
    {
      XDrawArc(dpy, p, gc, 1, 1, width - 2, height - 2, 0, 360 * 64);
      XFillArc(dpy, p, gc, 2, 2, width - 4, height - 4, 0, 360 * 64);
    }
  XShapeCombineMask(dpy, XtWindow(w), ShapeBounding,
		    -(int)w->core.border_width, -(int)w->core.border_width,
		    p, ShapeSet);
  if (w->core.border_width)
    {
      XSetForeground(dpy, gc, 0);
      XFillRectangle(dpy, p, gc, 0, 0, width, height);
      XSetForeground(dpy, gc, 1);
	if (!ellipse)
	XmuFillRoundedRectangle(dpy, p, gc, 1, 1,
				w->core.width - 2, w->core.height - 2,
				ew, eh);
      else
	XFillArc(dpy, p, gc, 0, 0, w->core.width, w->core.height,
		 0, 360 * 64);
      XShapeCombineMask(dpy, XtWindow(w), ShapeClip, 0, 0, p, ShapeSet);
    }
  else
    XShapeCombineMask(XtDisplay(w), XtWindow(w),
		      ShapeClip, 0, 0, None, ShapeSet);

  XFreePixmap(dpy, p);
  XFreeGC(dpy, gc);
}
