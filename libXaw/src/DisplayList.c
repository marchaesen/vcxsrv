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
 *
 * Author: Paulo César Pereira de Andrade
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/CoreP.h>
#include <X11/Xfuncs.h>
#include <X11/Xmu/CharSet.h>
#include "Private.h"


#ifndef OLDXAW

/*
 * Types
 */
typedef struct _XawDLProc XawDLProc;
typedef struct _XawDLData XawDLData;
typedef struct _XawDLInfo XawDLInfo;

struct _XawDLProc {
  XrmQuark qname;
  String *params;
  Cardinal num_params;
  XawDisplayListProc proc;
  XtPointer args;
  XawDLData *data;
};

struct _XawDLData {
  XawDLClass *dlclass;
  XtPointer data;
};

struct _XawDLInfo {
  String name;
  XrmQuark qname;
  XawDisplayListProc proc;
};

struct _XawDL {
  XawDLProc **procs;
  Cardinal num_procs;
  XawDLData **data;
  Cardinal num_data;
  Screen *screen;
  Colormap colormap;
  int depth;
  XrmQuark qrep;  /* for cache lookup */
};

struct _XawDLClass {
  String name;
  XawDLInfo **infos;
  Cardinal num_infos;
  XawDLArgsInitProc args_init;
  XawDLArgsDestructor args_destructor;
  XawDLDataInitProc data_init;
  XawDLDataDestructor data_destructor;
};

/*
 * Private Methods
 */
static XawDLClass *_XawFindDLClass(String);
static int qcmp_dlist_class(_Xconst void*, _Xconst void*);
static int bcmp_dlist_class(_Xconst void*, _Xconst void*);
static XawDLInfo *_XawFindDLInfo(XawDLClass*, String);
static int qcmp_dlist_info(_Xconst void*, _Xconst void*);
static int bcmp_dlist_info(_Xconst void*, _Xconst void*);
static void *_Xaw_Xlib_ArgsInitProc(String, String*, Cardinal*,
				    Screen*, Colormap, int);
static void _Xaw_Xlib_ArgsDestructor(Display*, String, XtPointer,
				     String*, Cardinal*);
static void *_Xaw_Xlib_DataInitProc(String, Screen*, Colormap, int);
static void _Xaw_Xlib_DataDestructor(Display*, String, XtPointer);

/*
 * Initialization
 */
static XawDLClass **classes;
static Cardinal num_classes;
static String xlib = "xlib";

/*
 * Implementation
 */
void
XawRunDisplayList(Widget w, _XawDisplayList *list,
		       XEvent *event, Region region)
{
  XawDLProc *proc;
  Cardinal i;

  if (!XtIsRealized(w))
    return;

  for (i = 0; i < list->num_procs; i++)
    {
      proc = list->procs[i];
      proc->proc(w, proc->args, proc->data->data, event, region);
    }
}

#define DLERR  -2
#define DLEOF  -1
#define DLEND  1
#define DLNAME 2
#define DLARG  3
static char *
read_token(char *src, char *dst, Cardinal size, int *status)
{
  int ch;
  Bool esc, quote;
  Cardinal i;

  i = 0;
  esc = quote = False;

  /*CONSTCOND*/
  while (1)
    {
      ch = *src;
      if (ch != '\n' && isspace(ch))
	++src;
      else
	break;
    }

  for (; i < size - 1; src++)
    {
      ch = *src;
      if (ch == '"')
	{
	  if (quote)
	    {
	      quote = False;
	      continue;
	    }
	  quote = True;
	  continue;
	}
      if (ch == '\\')
	{
	  if (esc)
	    {
	      dst[i++] = ch;
	      esc = False;
	      continue;
	    }
	  esc = True;
	  continue;
	}
      if (ch == '\0')
	{
	  *status = DLEOF;
	  dst[i] = '\0';
	  return (src);
	}
      else if (!esc)
	{
	  if (!quote)
	    {
	      if (ch == ',')
		{
		  *status = DLARG;
		  dst[i] = '\0';
		  return (++src);
		}
	      else if (ch == ' ' || ch == '\t')
		{
		  *status = DLNAME;
		  dst[i] = '\0';
		  return (++src);
		}
	      else if (ch == ';' || ch == '\n')
		{
		  *status = DLEND;
		  dst[i] = '\0';
		  return (++src);
		}
	    }
	}
      else
	esc = False;
      dst[i++] = ch;
    }

  *status = DLERR;
  dst[i] = '\0';

  return (src);
}

_XawDisplayList *XawCreateDisplayList(String string, Screen *screen,
				     Colormap colormap, int depth)
{
  _XawDisplayList *dlist;
  XawDLClass *lc, *xlibc;
  XawDLData *data;
  XawDLInfo *info;
  XawDLProc *proc;
  char cname[64], fname[64], aname[1024];
  Cardinal i;
  char *cp, *fp, *lp;
  int status;

  xlibc = XawGetDisplayListClass(xlib);
  if (!xlibc)
    {
      XawDisplayListInitialize();
      xlibc = XawGetDisplayListClass(xlib);
    }

  dlist = (_XawDisplayList *)XtMalloc(sizeof(_XawDisplayList));
  dlist->procs = NULL;
  dlist->num_procs = 0;
  dlist->data = NULL;
  dlist->num_data = 0;
  dlist->screen = screen;
  dlist->colormap = colormap;
  dlist->depth = depth;
  dlist->qrep = NULLQUARK;
  if (!string || !string[0])
    return (dlist);

  cp = string;

  status = 0;
  while (status != DLEOF)
    {
      lp = cp;
      cp = read_token(cp, fname, sizeof(fname), &status);

      if (status != DLNAME && status != DLEND && status != DLEOF)
	{
	  char msg[256];

	  snprintf(msg, sizeof(msg),
		   "Error parsing displayList at \"%s\"", lp);
	  XtAppWarning(XtDisplayToApplicationContext(DisplayOfScreen(screen)),
		       msg);
	  XawDestroyDisplayList(dlist);
	  return (NULL);
	}
      fp = fname;
      /*CONSTCOND*/
      while (1)
	{
	  fp = strchr(fp, ':');
	  if (!fp || (fp == cp || fp[-1] != '\\'))
	    break;
	  ++fp;
	}
      if (fp)
	{
	  snprintf(cname, fp - fname + 1, "%s", fname);
	  memmove(fname, fp + 1, strlen(fp));
	  lc = cname[0] ? XawGetDisplayListClass(cname) : xlibc;
	  if (!lc)
	    {
	      char msg[256];

	      snprintf(msg, sizeof(msg),
		       "Cannot find displayList class \"%s\"", cname);
	      XtAppWarning(XtDisplayToApplicationContext
			   (DisplayOfScreen(screen)), msg);
	      XawDestroyDisplayList(dlist);
	      return (NULL);
	    }
	}
      else
	lc = xlibc;

      if (status == DLEOF && !fname[0])
	break;

      if ((info = _XawFindDLInfo(lc, fname)) == NULL)
	{
	  char msg[256];

	  snprintf(msg, sizeof(msg),
		   "Cannot find displayList procedure \"%s\"", fname);
	  XtAppWarning(XtDisplayToApplicationContext(DisplayOfScreen(screen)),
		       msg);
	  XawDestroyDisplayList(dlist);
	  return (NULL);
	}

      proc = (XawDLProc *)XtMalloc(sizeof(XawDLProc));
      proc->qname = info->qname;
      proc->params = NULL;
      proc->num_params = 0;
      proc->proc = info->proc;
      proc->args = NULL;
      proc->data = NULL;

      if (!dlist->procs)
	{
	  dlist->num_procs = 1;
	  dlist->procs = (XawDLProc**)XtMalloc(sizeof(XawDLProc*));
	}
      else
	{
	  ++dlist->num_procs;
	  dlist->procs = (XawDLProc**)
	    XtRealloc((char *)dlist->procs, sizeof(XawDLProc*) *
		      dlist->num_procs);
	}
      dlist->procs[dlist->num_procs - 1] = proc;

      while (status != DLEND && status != DLEOF)
	{
	  lp = cp;
	  cp = read_token(cp, aname, sizeof(aname), &status);

	  if (status != DLARG && status != DLEND && status != DLEOF)
	    {
	      char msg[256];

	      snprintf(msg, sizeof(msg),
		       "Error parsing displayList at \"%s\"", lp);
	      XtAppWarning(XtDisplayToApplicationContext
			   (DisplayOfScreen(screen)), msg);
	      XawDestroyDisplayList(dlist);
	      return (NULL);
	    }

	  if (!proc->num_params)
	    {
	      proc->num_params = 1;
	      proc->params = (String *)XtMalloc(sizeof(String));
	    }
	  else
	    {
	      ++proc->num_params;
	      proc->params = (String *)XtRealloc((char *)proc->params,
						 sizeof(String) *
						 proc->num_params);
	    }
	  proc->params[proc->num_params - 1] = XtNewString(aname);
	}

      /* verify if data is already created for lc */
      data = NULL;
      for (i = 0; i < dlist->num_data; i++)
	if (dlist->data[i]->dlclass == lc)
	  {
	    data = dlist->data[i];
	    break;
	  }

      if (!data)
	{
	  data = (XawDLData *)XtMalloc(sizeof(XawDLData));
	  data->dlclass = lc;
	  if (lc->data_init)
	    data->data = lc->data_init(lc->name, screen, colormap, depth);
	  else
	    data->data = NULL;

	  if (!dlist->data)
	    {
	      dlist->num_data = 1;
	      dlist->data = (XawDLData **)XtMalloc(sizeof(XawDLData*));
	    }
	  else
	    {
	      ++dlist->num_data;
	      dlist->data = (XawDLData **)
		XtRealloc((char *)dlist->data, sizeof(XawDLData*) *
			  dlist->num_data);
	    }
	  dlist->data[dlist->num_data - 1] = data;
	}

      if (lc->args_init)
	{
	  proc->args = lc->args_init(fname, proc->params, &proc->num_params,
				    screen, colormap, depth);
	  if (proc->args == XAWDL_CONVERT_ERROR)
	    {
	      char msg[256];

	      proc->args = NULL;
	      snprintf(msg, sizeof(msg),
		       "Cannot convert arguments to displayList function \"%s\"",
		       fname);
	      XtAppWarning(XtDisplayToApplicationContext
			   (DisplayOfScreen(screen)), msg);
	      XawDestroyDisplayList(dlist);
	      return (NULL);
	    }
	}
      else
	proc->args = NULL;

      proc->data = data;
    }

  dlist->qrep = XrmStringToQuark(string);
  return (dlist);
}

String
XawDisplayListString(_XawDisplayList *dlist)
{
  if (!dlist || dlist->qrep == NULLQUARK)
    return ("");
  return (XrmQuarkToString(dlist->qrep));
}

void
XawDestroyDisplayList(_XawDisplayList *dlist)
{
  Cardinal i, j;
  XawDLProc *proc;
  XawDLData *data;

  if (!dlist)
    return;

  for (i = 0; i < dlist->num_procs; i++)
    {
      proc = dlist->procs[i];
      data = proc->data;

      if (data)
	{
	  if (data->dlclass->args_destructor)
	    data->dlclass->args_destructor(DisplayOfScreen(dlist->screen),
					   XrmQuarkToString(proc->qname),
					   proc->args,
					   proc->params, &proc->num_params);
	  if (data->data)
	    {
	      if (data->dlclass->data_destructor)
		{
		  data->dlclass
		    ->data_destructor(DisplayOfScreen(dlist->screen),
				      data->dlclass->name,  data->data);
		  data->data = NULL;
		}
	    }
	}

      for (j = 0; j < proc->num_params; j++)
	XtFree(proc->params[j]);
      if (proc->num_params)
	XtFree((char *)proc->params);
      XtFree((char *)proc);
    }

  if (dlist->num_procs)
    XtFree((char *)dlist->procs);

  XtFree((char *)dlist);
}

/**********************************************************************
 * If you want to implement your own class of procedures, look at
 * the code bellow.
 **********************************************************************/
/* Start of Implementation of class "xlib" */
typedef struct _XawXlibData {
  GC gc;
  unsigned long mask;
  XGCValues values;
  int shape;
  int mode;
  char *dashes;
  /* these fields can be used for optimization, to
   * avoid unnecessary coordinates recalculation.
   */
  Position x, y;
  Dimension width, height;
} XawXlibData;

typedef struct _XawDLPosition {
  Position pos;
  short denom;
  Boolean high;
} XawDLPosition;

typedef struct _XawDLPositionPtr {
  XawDLPosition *pos;
  Cardinal num_pos;
} XawDLPositionPtr;

typedef struct _XawDLArcArgs {
  XawDLPosition pos[4];
  int angle1;
  int angle2;
} XawDLArcArgs;

typedef struct _XawDLStringArgs {
  XawDLPosition pos[2];
  char *string;
  int length;
} XawDLStringArgs;

typedef struct _XawDLCopyArgs {
  XawPixmap *pixmap;
  XawDLPosition pos[6];
  int plane;
} XawDLCopyArgs;

typedef struct _XawDLImageArgs {
  XawPixmap *pixmap;
  XawDLPosition pos[4];
  int depth;
} XawDLImageArgs;

#define X_ARG(x) (Position)(((x).denom != 0) ?				      \
		  ((float)XtWidth(w) * ((float)(x).pos / (float)(x).denom)) : \
		  ((x).high ? XtWidth(w) - (x).pos : (x).pos))
#define Y_ARG(x) (Position)(((x).denom != 0) ?				      \
		  ((float)XtHeight(w) * ((float)(x).pos / (float)(x).denom)): \
		  ((x).high ? XtHeight(w) - (x).pos : (x).pos))
#define DRECT		0
#define FRECT		1
#define LINE		2
#define GCFG		3
#define GCBG		4
#define FPOLY		5
#define DARC		6
#define FARC		7
#define DLINES		8
#define MASK		9
#define UMASK		10
#define LWIDTH		11
#define POINT		12
#define POINTS		13
#define SEGMENTS	14
#define ARCMODE		15
#define COORDMODE	16
#define SHAPEMODE	17
#define LINESTYLE	18
#define CAPSTYLE	19
#define JOINSTYLE	20
#define FILLSTYLE	21
#define FILLRULE	22
#define	TILE		23
#define STIPPLE		24
#define TSORIGIN	25
#define FUNCTION	26
#define PLANEMASK	27
#define DSTRING		28
#define PSTRING		29
#define FONT		30
#define DASHES		31
#define SUBWMODE	32
#define EXPOSURES	33
#define CLIPORIGIN	34
#define CLIPMASK	35
#define CLIPRECTS	36
#define COPYAREA	37
#define COPYPLANE	38
#define IMAGE		39

static void
Dl1Point(Widget w, XtPointer args, XtPointer data, int id)
{
  XawDLPosition *pos = (XawDLPosition *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  Display *display;
  Window window;
  Position x, y;

  x = X_ARG(pos[0]);
  y = Y_ARG(pos[1]);

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      x += xpad;
      y += ypad;
      display = XtDisplayOfObject(w);
      window = XtWindowOfObject(w);
    }
  else
    {
      display = XtDisplay(w);
      window = XtWindow(w);
    }

  if (id == POINT)
    XDrawPoint(display, window, xdata->gc, x, y);
  else if (id == TSORIGIN)
    {
      xdata->values.ts_x_origin = x;
      xdata->values.ts_y_origin = y;
      xdata->mask |= GCTileStipXOrigin | GCTileStipYOrigin;
      XSetTSOrigin(display, xdata->gc, x, y);
    }
  else if (id == CLIPORIGIN)
    {
      xdata->values.clip_x_origin = x;
      xdata->values.clip_y_origin = y;
      xdata->mask |= GCClipXOrigin | GCClipYOrigin;
      XSetClipOrigin(display, xdata->gc, x, y);
    }
}

static void
Dl2Points(Widget w, XtPointer args, XtPointer data, int id)
{
  XawDLPosition *pos = (XawDLPosition *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  Display *display;
  Window window;
  Position x1, y1, x2, y2;

  x1 = X_ARG(pos[0]);
  y1 = Y_ARG(pos[1]);
  x2 = X_ARG(pos[2]);
  y2 = Y_ARG(pos[3]);

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      x1 += xpad; y1 += ypad;
      x2 += xpad; y2 += ypad;
      display = XtDisplayOfObject(w);
      window = XtWindowOfObject(w);
    }
  else
    {
      display = XtDisplay(w);
      window = XtWindow(w);
    }

  if (id == DRECT)
    XDrawRectangle(display, window, xdata->gc, x1, y1, x2 - x1, y2 - y1);
  else if (id == FRECT)
    XFillRectangle(display, window, xdata->gc, x1, y1, x2 - x1, y2 - y1);
  else if (id == LINE)
    XDrawLine(display, window, xdata->gc, x1, y1, x2, y2);
}

/* ARGSUSED */
static void
DlLine(Widget w, XtPointer args, XtPointer data, XEvent *event, Region region)
{
  Dl2Points(w, args, data, LINE);
}

/* ARGSUSED */
static void
DlDrawRectangle(Widget w, XtPointer args, XtPointer data,
		XEvent *event, Region region)
{
  Dl2Points(w, args, data, DRECT);
}

/* ARGSUSED */
static void
DlFillRectangle(Widget w, XtPointer args, XtPointer data,
		XEvent *event, Region region)
{
  Dl2Points(w, args, data, FRECT);
}

static void
DlXPoints(Widget w, XtPointer args, XtPointer data, int id)
{
  XawDLPositionPtr *pos_ptr = (XawDLPositionPtr *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  XawDLPosition *pos;
  XPoint points_buf[16];
  XPoint *points;
  Display *display;
  Window window;
  Cardinal num_points, i, j;

  num_points = pos_ptr->num_pos>>1;
  points = (XPoint *)XawStackAlloc(sizeof(XPoint) * num_points, points_buf);

  for (i = j = 0; i < num_points; i++, j = i << 1)
    {
      pos = &pos_ptr->pos[j];
      points[i].x = X_ARG(pos[0]);
      points[i].y = Y_ARG(pos[1]);
    }

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      if (xdata->mode != CoordModePrevious)
	{
	  for (i = 0; i < num_points; i++)
	    {
	      points[i].x += xpad;
	      points[i].y += ypad;
	    }
	}
      else
	{
	  points[0].x += xpad;
	  points[0].y += ypad;
	}
      display = XtDisplayOfObject(w);
      window = XtWindowOfObject(w);
    }
  else
    {
      display = XtDisplay(w);
      window = XtWindow(w);
    }

  if (id == FPOLY)
    XFillPolygon(display, window, xdata->gc, points, num_points,
		 xdata->shape, xdata->mode);
  else if (id == DLINES)
    XDrawLines(display, window, xdata->gc, points, num_points, xdata->mode);
  else if (id == POINTS)
    XDrawPoints(display, window, xdata->gc, points, num_points, xdata->mode);

  XawStackFree(points, points_buf);
}

/* ARGSUSED */
static void
DlFillPolygon(Widget w, XtPointer args, XtPointer data,
	      XEvent *event, Region region)
{
  DlXPoints(w, args, data, FPOLY);
}

/* ARGSUSED */
static void
DlDrawLines(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  DlXPoints(w, args, data, DLINES);
}

/* ARGSUSED */
static void
DlDrawPoints(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  DlXPoints(w, args, data, POINTS);
}

/* ARGSUSED */
static void
DlForeground(Widget w, XtPointer args, XtPointer data,
	     XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  Pixel foreground = (Pixel)args;

  if (xdata->values.foreground != foreground)
    {
      xdata->mask |= GCForeground;
      xdata->values.foreground = foreground;
      XSetForeground(XtDisplayOfObject(w), xdata->gc, foreground);
    }
}

/* ARGSUSED */
static void
DlBackground(Widget w, XtPointer args, XtPointer data,
	     XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  Pixel background = (Pixel)args;

  if (xdata->values.background != background)
    {
      xdata->mask |= GCBackground;
      xdata->values.background = background;
      XSetBackground(XtDisplayOfObject(w), xdata->gc, background);
    }
}

static void
DlArc(Widget w, XtPointer args, XtPointer data, Bool fill)
{
  XawXlibData *xdata = (XawXlibData *)data;
  XawDLArcArgs *arc = (XawDLArcArgs *)args;
  Position x1, y1, x2, y2;
  Display *display;
  Window window;

  x1 = X_ARG(arc->pos[0]);
  y1 = Y_ARG(arc->pos[1]);
  x2 = X_ARG(arc->pos[2]);
  y2 = Y_ARG(arc->pos[3]);

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      x1 += xpad;
      y1 += ypad;
      x2 += xpad;
      y2 += ypad;
      display = XtDisplayOfObject(w);
      window = XtWindowOfObject(w);
    }
  else
    {
      display = XtDisplay(w);
      window = XtWindow(w);
    }

  if (fill)
    XFillArc(display, window, xdata->gc, x1, y1, x2 - x1, y2 - y1,
	     arc->angle1, arc->angle2);
  else
    XDrawArc(display, window, xdata->gc, x1, y1, x2 - x1, y2 - y1,
	     arc->angle1, arc->angle2);
}

/* ARGSUSED */
static void
DlDrawArc(Widget w, XtPointer args, XtPointer data,
	  XEvent *event, Region region)
{
  DlArc(w, args, data, False);
}

/* ARGSUSED */
static void
DlFillArc(Widget w, XtPointer args, XtPointer data,
	  XEvent *event, Region region)
{
  DlArc(w, args, data, True);
}

/*ARGSUSED*/
static void
DlMask(Widget w, XtPointer args, XtPointer data,
       XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  Display *display = XtDisplayOfObject(w);

  if (region)
    XSetRegion(display, xdata->gc, region);
  else if (event)
    {
      XRectangle rect;

      rect.x = event->xexpose.x;
      rect.y = event->xexpose.y;
      rect.width = event->xexpose.width;
      rect.height = event->xexpose.height;
      XSetClipRectangles(display, xdata->gc, 0, 0, &rect, 1, Unsorted);
    }
}

/* ARGSUSED */
static void
DlUmask(Widget w, XtPointer args, XtPointer data,
	XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;

  XSetClipMask(XtDisplayOfObject(w), xdata->gc, None);
}

/* ARGSUSED */
static void
DlLineWidth(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  unsigned line_width = (unsigned long)args;

  if (xdata->values.line_width != line_width)
    {
      xdata->mask |= GCLineWidth;
      xdata->values.line_width = line_width;
      XChangeGC(XtDisplayOfObject(w), xdata->gc, GCLineWidth, &xdata->values);
    }
}

/* ARGSUSED */
static void
DlDrawPoint(Widget w, XtPointer args, XtPointer data, XEvent *event, Region region)
{
  Dl1Point(w, args, data, POINT);
}

/* ARGSUSED */
static void
DlDrawSegments(Widget w, XtPointer args, XtPointer data,
	       XEvent *event, Region region)
{
  XawDLPositionPtr *pos_ptr = (XawDLPositionPtr *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  XawDLPosition *pos;
  XSegment *segments;
  XSegment segments_buf[8];
  Display *display;
  Window window;
  Cardinal num_segments, i, j;

  num_segments = pos_ptr->num_pos>>2;
  segments = (XSegment *)XawStackAlloc(sizeof(XSegment) * num_segments, segments_buf);

  for (i = j = 0; i < num_segments; i++, j = i << 2)
    {
      pos = &pos_ptr->pos[j];
      segments[i].x1 = X_ARG(pos[0]);
      segments[i].y1 = Y_ARG(pos[1]);
      segments[i].x2 = X_ARG(pos[2]);
      segments[i].y2 = Y_ARG(pos[3]);
    }

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      for (i = 0; i < num_segments; i++)
	{
	  segments[i].x1 += xpad;
	  segments[i].y1 += ypad;
	  segments[i].x2 += xpad;
	  segments[i].y2 += ypad;
	}
      display = XtDisplayOfObject(w);
      window = XtWindowOfObject(w);
    }
  else
    {
      display = XtDisplay(w);
      window = XtWindow(w);
    }

  XDrawSegments(display, window, xdata->gc, segments, num_segments);

  XawStackFree(segments, segments_buf);
}

/* ARGSUSED */
static void
DlArcMode(Widget w, XtPointer args, XtPointer data,
	  XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int arc_mode  = (long)args;

  if (xdata->values.arc_mode != arc_mode)
    {
      xdata->mask |= GCArcMode;
      xdata->values.arc_mode = arc_mode;
      XSetArcMode(XtDisplayOfObject(w), xdata->gc, arc_mode);
    }
}

/* ARGSUSED */
static void
DlCoordMode(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int mode  = (long)args;

  xdata->mode = mode;
}

/* ARGSUSED */
static void
DlShapeMode(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int shape  = (long)args;

  xdata->shape = shape;
}

/* ARGSUSED */
static void
DlLineStyle(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int line_style = (long)args;

  if (xdata->values.line_style != line_style)
    {
      xdata->mask |= GCLineStyle;
      xdata->values.line_style = line_style;
      XChangeGC(XtDisplayOfObject(w), xdata->gc, GCLineStyle, &xdata->values);
    }
}

/* ARGSUSED */
static void
DlCapStyle(Widget w, XtPointer args, XtPointer data,
	   XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int cap_style = (long)args;

  if (xdata->values.cap_style != cap_style)
    {
      xdata->mask |= GCCapStyle;
      xdata->values.cap_style = cap_style;
      XChangeGC(XtDisplayOfObject(w), xdata->gc, GCCapStyle, &xdata->values);
    }
}

/* ARGSUSED */
static void
DlJoinStyle(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int join_style = (long)args;

  if (xdata->values.join_style != join_style)
    {
      xdata->mask |= GCJoinStyle;
      xdata->values.join_style = join_style;
      XChangeGC(XtDisplayOfObject(w), xdata->gc, GCJoinStyle, &xdata->values);
    }
}

/* ARGSUSED */
static void
DlFillStyle(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int fill_style = (long)args;

  if (xdata->values.fill_style != fill_style)
    {
      xdata->mask |= GCFillStyle;
      xdata->values.fill_style = fill_style;
      XSetFillStyle(XtDisplayOfObject(w), xdata->gc, fill_style);
    }
}

/* ARGSUSED */
static void
DlFillRule(Widget w, XtPointer args, XtPointer data,
	   XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int fill_rule = (long)args;

  if (xdata->values.fill_rule != fill_rule)
    {
      xdata->mask |= GCFillRule;
      xdata->values.fill_rule = fill_rule;
      XSetFillRule(XtDisplayOfObject(w), xdata->gc, fill_rule);
    }
}

/* ARGSUSED */
static void
DlTile(Widget w, XtPointer args, XtPointer data,
       XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  XawPixmap *pixmap = (XawPixmap *)args;

  if (pixmap && xdata->values.tile != pixmap->pixmap)
    {
      xdata->mask |= GCTile;
      xdata->values.tile = pixmap->pixmap;
      XSetTile(XtDisplayOfObject(w), xdata->gc, xdata->values.tile);
    }
}

/* ARGSUSED */
static void
DlStipple(Widget w, XtPointer args, XtPointer data,
	  XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  XawPixmap *pixmap = (XawPixmap *)args;

  if (pixmap && xdata->values.stipple != pixmap->pixmap)
    {
      xdata->mask |= GCStipple;
      xdata->values.stipple = pixmap->pixmap;
      XSetStipple(XtDisplayOfObject(w), xdata->gc, xdata->values.stipple);
    }
}

/* ARGSUSED */
static void
DlTSOrigin(Widget w, XtPointer args, XtPointer data, XEvent *event, Region region)
{
  Dl1Point(w, args, data, TSORIGIN);
}

/* ARGSUSED */
static void
DlFunction(Widget w, XtPointer args, XtPointer data,
	   XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int function = (long)args;

  if (function != xdata->values.function)
    {
      xdata->mask |= GCFunction;
      xdata->values.function = function;
      XSetFunction(XtDisplayOfObject(w), xdata->gc, function);
    }
}

/* ARGSUSED */
static void
DlPlaneMask(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  unsigned long plane_mask = (unsigned long)args;

  if (xdata->values.plane_mask != plane_mask)
    {
      xdata->mask |= GCPlaneMask;
      xdata->values.plane_mask = plane_mask;
      XSetPlaneMask(XtDisplayOfObject(w), xdata->gc, plane_mask);
    }
}

static void
DlString(Widget w, XtPointer args, XtPointer data, Bool image)
{
  XawDLStringArgs *string = (XawDLStringArgs *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  Display *display;
  Window window;
  Position x, y;

  x = X_ARG(string->pos[0]);
  y = Y_ARG(string->pos[1]);

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      x += xpad;
      y += ypad;
      display = XtDisplayOfObject(w);
      window = XtWindowOfObject(w);
    }
  else
    {
      display = XtDisplay(w);
      window = XtWindow(w);
    }

  if (image)
    XDrawImageString(display, window, xdata->gc, x, y, string->string, string->length);
  else
    XDrawString(display, window, xdata->gc, x, y, string->string, string->length);
}

/* ARGSUSED */
static void
DlDrawString(Widget w, XtPointer args, XtPointer data,
	     XEvent *event, Region region)
{
  DlString(w, args, data, False);
}

/* ARGSUSED */
static void
DlPaintString(Widget w, XtPointer args, XtPointer data,
	      XEvent *event, Region region)
{
  DlString(w, args, data, True);
}

/* ARGSUSED */
static void
DlFont(Widget w, XtPointer args, XtPointer data,
       XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  Font font = (Font)args;

  if (xdata->values.font != font)
    {
      xdata->mask |= GCFont;
      xdata->values.font = font;
      XSetFont(XtDisplayOfObject(w), xdata->gc, font);
    }
}

/* ARGSUSED */
static void
DlDashes(Widget w, XtPointer args, XtPointer data,
	 XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  char *dashes = args;

  if (xdata->dashes != dashes)
    {
      xdata->mask |= GCDashOffset | GCDashList;
      xdata->dashes = dashes;
      XSetDashes(XtDisplayOfObject(w), xdata->gc, 0, dashes + 1, *dashes);
    }
}

/* ARGSUSED */
static void
DlSubwindowMode(Widget w, XtPointer args, XtPointer data,
		XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  int subwindow_mode = (long)args;

  if (xdata->values.subwindow_mode != subwindow_mode)
    {
      xdata->mask |= GCSubwindowMode;
      xdata->values.subwindow_mode = subwindow_mode;
      XSetSubwindowMode(XtDisplayOfObject(w), xdata->gc, subwindow_mode);
    }
}

/* ARGSUSED */
static void
DlExposures(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  Bool graphics_exposures = (Bool)(long)args;

  if (xdata->values.graphics_exposures != graphics_exposures)
    {
      xdata->mask |= GCGraphicsExposures;
      xdata->values.graphics_exposures = graphics_exposures;
      XSetGraphicsExposures(XtDisplayOfObject(w), xdata->gc, graphics_exposures);
    }
}

/* ARGSUSED */
static void
DlClipOrigin(Widget w, XtPointer args, XtPointer data, XEvent *event, Region region)
{
  Dl1Point(w, args, data, CLIPORIGIN);
}

/* ARGSUSED */
static void
DlClipMask(Widget w, XtPointer args, XtPointer data,
	   XEvent *event, Region region)
{
  XawXlibData *xdata = (XawXlibData *)data;
  XawPixmap *pixmap = (XawPixmap *)args;
  Pixmap clip_mask;

  if (pixmap)
    clip_mask = pixmap->mask ? pixmap->mask : pixmap->pixmap;
  else
    clip_mask = None;

  if (xdata->values.clip_mask != clip_mask)
    {
      xdata->mask |= GCClipMask;
      XSetClipMask(XtDisplayOfObject(w), xdata->gc, clip_mask);
    }
}

/* ARGSUSED */
static void
DlClipRectangles(Widget w, XtPointer args, XtPointer data,
		 XEvent *event, Region region)
{
  XawDLPositionPtr *pos_ptr = (XawDLPositionPtr *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  XawDLPosition *pos;
  XRectangle *rects;
  XRectangle rects_buf[8];
  Position x1, y1, x2, y2;
  Cardinal num_rects, i, j;

  num_rects = pos_ptr->num_pos>>2;
  rects = (XRectangle *)XawStackAlloc(sizeof(XRectangle) * num_rects, rects_buf);

  for (i = j = 0; i < num_rects; i++, j = i << 2)
    {
      pos = &pos_ptr->pos[j];
      x1 = X_ARG(pos[0]);
      y1 = Y_ARG(pos[1]);
      x2 = X_ARG(pos[2]);
      y2 = Y_ARG(pos[3]);
      rects[i].x = XawMin(x1, x2);
      rects[i].y = XawMin(y1, y2);
      rects[i].width = XawMax(x1, x2) - rects[i].x;
      rects[i].height = XawMax(y1, y2) - rects[i].y;
    }

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      for (i = 0; i < num_rects; i++)
	{
	  rects[i].x += xpad;
	  rects[i].y += ypad;
	}
    }

  XSetClipRectangles(XtDisplayOfObject(w), xdata->gc, 0, 0, rects, num_rects, Unsorted);

  XawStackFree(rects, rects_buf);
}

static void
DlCopy(Widget w, XtPointer args, XtPointer data, Bool plane)
{
  XawDLCopyArgs *copy = (XawDLCopyArgs *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  int src_x, src_y, dst_x, dst_y, width, height, tmp1, tmp2;

  tmp1 = X_ARG(copy->pos[0]);
  tmp2 = X_ARG(copy->pos[2]);
  dst_x = XawMin(tmp1, tmp2);
  width = XawMax(tmp1, tmp2) - dst_x;

  tmp1 = Y_ARG(copy->pos[1]);
  tmp2 = Y_ARG(copy->pos[3]);
  dst_y = XawMin(tmp1, tmp2);
  height = XawMax(tmp1, tmp2) - dst_y;

  src_x = X_ARG(copy->pos[4]);
  src_y = Y_ARG(copy->pos[5]);

  if (width <= 0)
    {
      if (copy->pixmap)
	width = copy->pixmap->width;
      else
	{
	  if ((width = XtWidth(w) - src_x) < 0)
	    width = 0;
	}
    }
  if (height <= 0)
    {
      if (copy->pixmap)
	height = copy->pixmap->height;
      else
	{
	  if ((height = XtHeight(w) - src_y) < 0)
	    height = 0;
	}
    }

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      src_x += xpad;
      src_y += ypad;
      dst_x += xpad;
      dst_y += ypad;
    }

  if (plane)
    XCopyPlane(XtDisplayOfObject(w), XtWindowOfObject(w),
	       copy->pixmap ? copy->pixmap->pixmap : XtWindowOfObject(w),
	       xdata->gc, src_x, src_y, width, height, dst_x, dst_y,
	       copy->plane ? copy->plane : 1);
  else
    XCopyArea(XtDisplayOfObject(w),
	      copy->pixmap ? copy->pixmap->pixmap : XtWindowOfObject(w),
	      XtWindowOfObject(w), xdata->gc, src_x, src_y, width, height, dst_x, dst_y);
}

/* ARGSUSED */
static void
DlCopyArea(Widget w, XtPointer args, XtPointer data,
	   XEvent *event, Region region)
{
  DlCopy(w, args, data, False);
}

/* ARGSUSED */
static void
DlCopyPlane(Widget w, XtPointer args, XtPointer data,
	    XEvent *event, Region region)
{
  DlCopy(w, args, data, True);
}

/*ARGSUSED*/
/* Note:
 *	  This function is destructive if you set the ts_x_origin, ts_y_origin,
 *	and/or clip-mask. It is meant to be the only function used in a display
 *	list. If you need to use other functions (and those values), be sure to
 *	set them after calling this function.
 */
static void
DlImage(Widget w, XtPointer args, XtPointer data, XEvent *event, Region region)
{
  XawDLImageArgs *image = (XawDLImageArgs *)args;
  XawXlibData *xdata = (XawXlibData *)data;
  int x, y, xs, ys, xe, ye, width, height;
  Display *display;
  Window window;

  width = image->pixmap->width;
  height = image->pixmap->height;
  xs = X_ARG(image->pos[0]);
  ys = Y_ARG(image->pos[1]);
  xe = X_ARG(image->pos[2]);
  ye = Y_ARG(image->pos[3]);

  if (xe <= 0)
    xe = xs + width;
  if (ye <= 0)
    ye = ys + height;

  if (!XtIsWidget(w))
    {
      Position xpad, ypad;

      xpad = XtX(w) + XtBorderWidth(w);
      ypad = XtY(w) + XtBorderWidth(w);
      xe += xpad;
      ye += ypad;
      xe += xpad;
      ye += ypad;
      display = XtDisplayOfObject(w);
      window = XtWindowOfObject(w);
    }
  else
    {
      display = XtDisplay(w);
      window = XtWindow(w);
    }

  for (y = ys; y < ye; y += height)
    for (x = xs; x < xe; x += width)
      {
	XSetClipOrigin(display, xdata->gc, x, y);
	if (image->pixmap->mask)
	  XSetClipMask(display, xdata->gc, image->pixmap->mask);
	if (image->depth == 1)
	  XCopyPlane(display, image->pixmap->pixmap, window, xdata->gc,
		     0, 0, XawMin(width, xe - x), XawMin(height, ye - y),
		     x, y, 1L);
	else
	  XCopyArea(display, image->pixmap->pixmap, window, xdata->gc, 0, 0,
		     XawMin(width, xe - x), XawMin(height, ye - y), x, y);
      }

  XSetClipMask(display, xdata->gc, None);
}

typedef struct _Dl_init Dl_init;
struct _Dl_init {
  String name;
  XawDisplayListProc proc;
  Cardinal id;
};

static Dl_init dl_init[] =
{
  {"arc-mode",		DlArcMode,		ARCMODE},
  {"background",	DlBackground,		GCBG},
  {"bg",		DlBackground,		GCBG},
  {"cap-style",		DlCapStyle,		CAPSTYLE},
  {"clip-mask",		DlClipMask,		CLIPMASK},
  {"clip-origin",	DlClipOrigin,		CLIPORIGIN},
  {"clip-rectangles",	DlClipRectangles,	CLIPRECTS},
  {"clip-rects",	DlClipRectangles,	CLIPRECTS},
  {"coord-mode",	DlCoordMode,		COORDMODE},
  {"copy-area",		DlCopyArea,		COPYAREA},
  {"copy-plane",	DlCopyPlane,		COPYPLANE},
  {"dashes",		DlDashes,		DASHES},
  {"draw-arc",		DlDrawArc,		DARC},
  {"draw-line",		DlLine,			LINE},
  {"draw-lines",	DlDrawLines,		DLINES},
  {"draw-point",	DlDrawPoint,		POINT},
  {"draw-points",	DlDrawPoints,		POINTS},
  {"draw-rect",		DlDrawRectangle,	DRECT},
  {"draw-rectangle",	DlDrawRectangle,	DRECT},
  {"draw-segments",	DlDrawSegments,		SEGMENTS},
  {"draw-string",	DlDrawString,		DSTRING},
  {"exposures",		DlExposures,		EXPOSURES},
  {"fg",		DlForeground,		GCFG},
  {"fill-arc",		DlFillArc,		FARC},
  {"fill-poly",		DlFillPolygon,		FPOLY},
  {"fill-polygon",	DlFillPolygon,		FPOLY},
  {"fill-rect",		DlFillRectangle,	FRECT},
  {"fill-rectangle",	DlFillRectangle,	FRECT},
  {"fill-rule",		DlFillRule,		FILLRULE},
  {"fill-style",	DlFillStyle,		FILLSTYLE},
  {"font",		DlFont,			FONT},
  {"foreground",	DlForeground,		GCFG},
  {"function",		DlFunction,		FUNCTION},
  {"image",		DlImage,		IMAGE},
  {"join-style",	DlJoinStyle,		JOINSTYLE},
  {"line",		DlLine,			LINE},
  {"line-style",	DlLineStyle,		LINESTYLE},
  {"line-width",	DlLineWidth,		LWIDTH},
  {"lines",		DlDrawLines,		DLINES},
  {"mask",		DlMask,			MASK},
  {"paint-string",	DlPaintString,		PSTRING},
  {"plane-mask",	DlPlaneMask,		PLANEMASK},
  {"point",		DlDrawPoint,		POINT},
  {"points",		DlDrawPoints,		POINTS},
  {"segments",		DlDrawSegments,		SEGMENTS},
  {"shape-mode",	DlShapeMode,		SHAPEMODE},
  {"stipple",		DlStipple,		STIPPLE},
  {"subwindow-mode",	DlSubwindowMode,	SUBWMODE},
  {"tile",		DlTile,			TILE},
  {"ts-origin",		DlTSOrigin,		TSORIGIN},
  {"umask",		DlUmask,		UMASK},
};

void
XawDisplayListInitialize(void)
{
  static Bool first_time = True;
  XawDLClass *lc;
  Cardinal i;

  if (first_time == False)
    return;

  first_time = False;

  lc = XawCreateDisplayListClass(xlib,
				 _Xaw_Xlib_ArgsInitProc,
				 _Xaw_Xlib_ArgsDestructor,
				 _Xaw_Xlib_DataInitProc,
				 _Xaw_Xlib_DataDestructor);
  for (i = 0; i < sizeof(dl_init) / sizeof(dl_init[0]); i++)
    (void)XawDeclareDisplayListProc(lc, dl_init[i].name, dl_init[i].proc);
}

static int
bcmp_cvt_proc(register _Xconst void *string,
	      register _Xconst void *dlinfo)
{
  return (strcmp((String)string, ((Dl_init*)dlinfo)->name));
}

static long
read_int(char *cp, char **cpp)
{
  long value = 0, sign = 1;

  if (*cp == '-')
    {
      sign = -1;
      ++cp;
    }
  else if (*cp == '+')
    ++cp;
  value = 0;
  while (*cp >= '0' && *cp <= '9')
    {
      value = value * 10 + *cp - '0';
      ++cp;
    }
  if (cpp)
    *cpp = cp;
  return (value * sign);
}

static void
read_position(char *arg, XawDLPosition *pos)
{
  int ch;
  char *str = arg;

  ch = *str;
  if (ch == '-' || ch == '+')
    {
      ++str;
      if (ch == '-')
	pos->high = True;
      pos->pos = read_int(str, NULL);
    }
  else if (isdigit(ch))
    {
      pos->pos = read_int(str, &str);
      ch = *str++;
      if (ch == '/')
	pos->denom = read_int(str, NULL);
    }
}

/* ARGSUSED */
static void *
_Xaw_Xlib_ArgsInitProc(String proc_name, String *params, Cardinal *num_params,
		       Screen *screen, Colormap colormap, int depth)
{
  Cardinal id, i;
  Dl_init *init;
  void *retval = XAWDL_CONVERT_ERROR;

  init = (Dl_init *)bsearch(proc_name, dl_init,
			    sizeof(dl_init) / sizeof(dl_init[0]),
			    sizeof(dl_init[0]),
			    bcmp_cvt_proc);

  id = init->id;

  switch (id)
    {
    case LINE:
    case DRECT:
    case FRECT:
      if (*num_params == 4)
	{
	  XawDLPosition *pos = (XawDLPosition *)XtCalloc(1, sizeof(XawDLPosition) * 4);

	  for (i = 0; i < 4; i++)
	    read_position(params[i], &pos[i]);
	  retval = (void *)pos;
	}
      break;
    case POINT:
    case TSORIGIN:
    case CLIPORIGIN:
      if (*num_params == 2)
	{
	  XawDLPosition *pos = (XawDLPosition *)XtCalloc(1, sizeof(XawDLPosition) * 2);

	  read_position(params[0], &pos[0]);
	  read_position(params[1], &pos[1]);
	  retval = (void *)pos;
	}
      break;
    case DLINES:
    case FPOLY:
    case POINTS:
      if (*num_params >= 4 && !(*num_params & 1))
	{
	  XawDLPositionPtr *pos = XtNew(XawDLPositionPtr);

	  pos->pos = (XawDLPosition *)XtCalloc(1, sizeof(XawDLPosition) *
					       *num_params);
	  pos->num_pos = *num_params;
	  for (i = 0; i < *num_params; i++)
	    read_position(params[i], &pos->pos[i]);
	  retval = (void *)pos;
	}
      break;
    case SEGMENTS:
    case CLIPRECTS:
      if (*num_params >= 4 && !(*num_params % 4))
	{
	  XawDLPositionPtr *pos = XtNew(XawDLPositionPtr);

	  pos->pos = (XawDLPosition *)XtCalloc(1, sizeof(XawDLPosition) *
					       *num_params);
	  pos->num_pos = *num_params;
	  for (i = 0; i < *num_params; i++)
	    read_position(params[i], &pos->pos[i]);
	  retval = (void *)pos;
	}
      break;
    case DARC:
    case FARC:
      if (*num_params >= 4 && *num_params <= 6)
	{
	  XawDLArcArgs *args = (XawDLArcArgs *)XtCalloc(1, sizeof(XawDLArcArgs));

	  args->angle1 = 0;
	  args->angle2 = 360;
	  for (i = 0; i < 4; i++)
	    read_position(params[i], &args->pos[i]);
	  if (*num_params > 4)
	    args->angle1 = read_int(params[4], NULL);
	  if (*num_params > 5)
	    args->angle2 = read_int(params[5], NULL);
	  args->angle1 *= 64;
	  args->angle2 *= 64;
	  retval = (void *)args;
	}
      break;
    case GCFG:
    case GCBG:
      {
	XColor xcolor;

	if (*num_params == 1 &&
	    XAllocNamedColor(DisplayOfScreen(screen), colormap,
			     params[0], &xcolor, &xcolor))
	  retval = (void *)xcolor.pixel;
      } break;
    case MASK:
    case UMASK:
      if (*num_params == 0)
	retval = NULL;
      break;
    case LWIDTH:
      if (*num_params == 1)
	retval = (void *)read_int(params[0], NULL);
      break;
    case ARCMODE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "pieslice") == 0)
	    retval = (void *)ArcPieSlice;
	  else if (XmuCompareISOLatin1(params[0], "chord") == 0)
	    retval = (void *)ArcChord;
	}
      break;
    case COORDMODE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "origin") == 0)
	    retval = (void *)CoordModeOrigin;
	  else if (XmuCompareISOLatin1(params[0], "previous") == 0)
	    retval = (void *)CoordModePrevious;
	}
      break;
    case SHAPEMODE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "complex") == 0)
	    retval = (void *)Complex;
	  else if (XmuCompareISOLatin1(params[0], "convex") == 0)
	    retval = (void *)Convex;
	  else if (XmuCompareISOLatin1(params[0], "nonconvex") == 0)
	    retval = (void *)Nonconvex;
	}
      break;
    case LINESTYLE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "solid") == 0)
	    retval = (void *)LineSolid;
	  else if (XmuCompareISOLatin1(params[0], "onoffdash") == 0)
	    retval = (void *)LineOnOffDash;
	  else if (XmuCompareISOLatin1(params[0], "doubledash") == 0)
	    retval = (void *)LineDoubleDash;
	}
      break;
    case CAPSTYLE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "notlast") == 0)
	    retval = (void *)CapNotLast;
	  else if (XmuCompareISOLatin1(params[0], "butt") == 0)
	    retval = (void *)CapButt;
	  else if (XmuCompareISOLatin1(params[0], "round") == 0)
	    retval = (void *)CapRound;
	  else if (XmuCompareISOLatin1(params[0], "projecting") == 0)
	    retval = (void *)CapProjecting;
	}
      break;
    case JOINSTYLE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "miter") == 0)
	    retval = (void *)JoinMiter;
	  else if (XmuCompareISOLatin1(params[0], "round") == 0)
	    retval = (void *)JoinRound;
	  else if (XmuCompareISOLatin1(params[0], "bevel") == 0)
	    retval = (void *)JoinBevel;
	}
      break;
    case FILLSTYLE:
      if (*num_params == 1)
	{
	  if (*num_params && XmuCompareISOLatin1(params[0], "solid") == 0)
	    retval = (void *)FillSolid;
	  else if (*num_params && XmuCompareISOLatin1(params[0], "tiled") == 0)
	    retval = (void *)FillTiled;
	  else if (*num_params && XmuCompareISOLatin1(params[0], "stippled") == 0)
	    retval = (void *)FillStippled;
	  else if (*num_params && XmuCompareISOLatin1(params[0], "opaquestippled") == 0)
	    retval = (void *)FillOpaqueStippled;
	}
      break;
    case FILLRULE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "evenodd") == 0)
	    retval = (void *)EvenOddRule;
	  else if (XmuCompareISOLatin1(params[0], "winding") == 0)
	    retval = (void *)WindingRule;
	}
      break;
    case TILE:
      if (*num_params == 1)
	retval = (void *)XawLoadPixmap(params[0], screen, colormap, depth);
      if (retval == NULL)
	{
	  XtDisplayStringConversionWarning(DisplayOfScreen(screen), (String)params[0],
					   XtRPixmap);
	  retval = XAWDL_CONVERT_ERROR;
	}
      break;
    case STIPPLE:
      if (*num_params == 1)
	retval = (void *)XawLoadPixmap(params[0], screen, colormap, 1);
      if (retval == NULL)
	{
	  XtDisplayStringConversionWarning(DisplayOfScreen(screen), (String)params[0],
					   XtRBitmap);
	  retval = XAWDL_CONVERT_ERROR;
	}
      break;
    case FUNCTION:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "set") == 0)
	    retval = (void *)GXset;
	  else if (XmuCompareISOLatin1(params[0], "clear") == 0)
	    retval = (void *)GXclear;
	  else if (XmuCompareISOLatin1(params[0], "and") == 0)
	    retval = (void *)GXand;
	  else if (XmuCompareISOLatin1(params[0], "andreverse") == 0)
	    retval = (void *)GXandReverse;
	  else if (XmuCompareISOLatin1(params[0], "copy") == 0)
	    retval = (void *)GXcopy;
	  else if (XmuCompareISOLatin1(params[0], "andinverted") == 0)
	    retval = (void *)GXandInverted;
	  else if (XmuCompareISOLatin1(params[0], "noop") == 0)
	    retval = (void *)GXnoop;
	  else if (XmuCompareISOLatin1(params[0], "xor") == 0)
	    retval = (void *)GXxor;
	  else if (XmuCompareISOLatin1(params[0], "or") == 0)
	    retval = (void *)GXor;
	  else if (XmuCompareISOLatin1(params[0], "nor") == 0)
	    retval = (void *)GXnor;
	  else if (XmuCompareISOLatin1(params[0], "equiv") == 0)
	    retval = (void *)GXequiv;
	  else if (XmuCompareISOLatin1(params[0], "invert") == 0)
	    retval = (void *)GXinvert;
	  else if (XmuCompareISOLatin1(params[0], "orreverse") == 0)
	    retval = (void *)GXorReverse;
	  else if (XmuCompareISOLatin1(params[0], "copyinverted") == 0)
	    retval = (void *)GXcopyInverted;
	  else if (XmuCompareISOLatin1(params[0], "nand") == 0)
	    retval = (void *)GXnand;
	}
      break;
    case PLANEMASK:
      if (*num_params == 1)
	retval = (void *)read_int(params[0], NULL);
      break;
    case DSTRING:
    case PSTRING:
      if (*num_params == 3)
	{
	  XawDLStringArgs *string = (XawDLStringArgs *)
		XtCalloc(1, sizeof(XawDLStringArgs));

	  read_position(params[0], &string->pos[0]);
	  read_position(params[1], &string->pos[1]);
	  string->string = XtNewString(params[2]);
	  string->length = strlen(string->string);
	  retval = string;
	}
      break;
    case FONT:
      if (*num_params == 1)
	retval = (void *)XLoadFont(DisplayOfScreen(screen), params[0]);
      break;
    case DASHES:
      if (*num_params && *num_params < 127)
	{
	  char *dashes;

	  dashes = XtMalloc(*num_params + 1);

	  for (i = 0; i < *num_params; i++)
	    dashes[i + 1] = read_int(params[i], NULL);
	  *dashes = *num_params;
	  retval = dashes;
	}
      break;
    case SUBWMODE:
      if (*num_params == 1)
	{
	  if (XmuCompareISOLatin1(params[0], "clipbychildren") == 0)
	    retval = (void *)ClipByChildren;
	  else if (XmuCompareISOLatin1(params[0], "includeinferiors") == 0)
	    retval = (void *)IncludeInferiors;
	}
      break;
    case EXPOSURES:
      if (*num_params == 1)
	{
	  if (isdigit(params[0][0]) || params[0][0] == '+' || params[0][0] == '-')
	    retval = (void *)read_int(params[0], NULL);
	  else if (XmuCompareISOLatin1(params[0], "true") == 0 ||
	    XmuCompareISOLatin1(params[0], "on") == 0)
	    retval = (void *)True;
	  else if (XmuCompareISOLatin1(params[0], "false") == 0 ||
	    XmuCompareISOLatin1(params[0], "off") == 0)
	    retval = (void *)False;
	}
      break;
    case CLIPMASK:
      if (*num_params == 1)
	retval = (void *)XawLoadPixmap(params[0], screen, colormap, 1);
      if (retval == NULL)
	{
	  retval = XAWDL_CONVERT_ERROR;
	  XtDisplayStringConversionWarning(DisplayOfScreen(screen), (String)params[0],
					   XtRPixmap);
	}
      break;
    case COPYAREA:
    case COPYPLANE:
      if (*num_params > 2 && *num_params <= 7 + (id == COPYPLANE))
	{
	  XawDLCopyArgs *args = (XawDLCopyArgs *)
		XtCalloc(1, sizeof(XawDLCopyArgs));

	  retval = args;
	  if (params[0][0] == '\0' || strcmp(params[0], ".") == 0)
	    args->pixmap = NULL;
	  else
	   {
	     args->pixmap = XawLoadPixmap(params[0], screen, colormap, id == COPYPLANE ? 1 : depth);
	     if (args->pixmap == NULL)
	      {
		XtDisplayStringConversionWarning(DisplayOfScreen(screen), (String)params[0],
						 XtRBitmap);
		retval = XAWDL_CONVERT_ERROR;
		XtFree((char *)args);
	      }
	  }
	  if (retval != XAWDL_CONVERT_ERROR)
	    {
	      for (i = 1; i < *num_params && i < 7; i++)
		read_position(params[i], &args->pos[i - 1]);
	      if (*num_params > 7)
		args->plane = read_int(params[7], NULL);
	    }
	}
      break;
    case IMAGE:
      if (*num_params > 2 && *num_params <= 7)
	{
	  XawDLImageArgs *args = (XawDLImageArgs *)
		XtCalloc(1, sizeof(XawDLImageArgs));

	  retval = args;
	  args->pixmap = XawLoadPixmap(params[0], screen, colormap, depth);
	  if (args->pixmap == NULL)
	    {
	      XtDisplayStringConversionWarning(DisplayOfScreen(screen),
					       (String)params[0], XtRPixmap);
	      retval = XAWDL_CONVERT_ERROR;
	      XtFree((char *)args);
	    }
	  else
	    {
	      args->depth = depth;
	      for (i = 1; i < *num_params && i < 5; i++)
		read_position(params[i], &args->pos[i - 1]);
	    }
	}
      break;
    }

  return (retval);
}

/* ARGSUSED */
static void *
_Xaw_Xlib_DataInitProc(String class_name,
		       Screen *screen, Colormap colormap, int depth)
{
  XawXlibData *data;
  Window tmp_win;

  data = (XawXlibData *)XtMalloc(sizeof(XawXlibData));

  tmp_win = XCreateWindow(DisplayOfScreen(screen),
			  RootWindowOfScreen(screen),
			  0, 0, 1, 1, 1, depth,
			  InputOutput, (Visual *)CopyFromParent, 0, NULL);
  data->mask = 0;
  data->gc = XCreateGC(DisplayOfScreen(screen), tmp_win, 0, &data->values);
  XDestroyWindow(DisplayOfScreen(screen), tmp_win);
  data->shape = Complex;
  data->mode = CoordModeOrigin;
  data->dashes = NULL;

  return ((void *)data);
}

/* ARGSUSED */
static void
_Xaw_Xlib_ArgsDestructor(Display *display, String proc_name, XtPointer args,
			 String *params, Cardinal *num_params)
{
  Cardinal id;
  Dl_init *init;

  init = (Dl_init *)bsearch(proc_name, dl_init,
			    sizeof(dl_init) / sizeof(dl_init[0]),
			    sizeof(dl_init[0]),
			    bcmp_cvt_proc);

  id = init->id;

  switch (id)
    {
    case LINE:
    case DRECT:
    case FRECT:
    case DARC:
    case FARC:
    case POINT:
    case TSORIGIN:
    case DASHES:
    case CLIPORIGIN:
    case COPYAREA:
    case COPYPLANE:
    case IMAGE:
      XtFree(args);
      break;
    case DSTRING:
    case PSTRING:
      {
	XawDLStringArgs *string = (XawDLStringArgs *)args;
	XtFree(string->string);
	XtFree(args);
      } break;
    case DLINES:
    case FPOLY:
    case POINTS:
    case SEGMENTS:
    case CLIPRECTS:
      {
	XawDLPositionPtr *ptr = (XawDLPositionPtr *)args;

	XtFree((char *)ptr->pos);
	XtFree(args);
      } break;
    }
}

/* ARGSUSED */
static void
_Xaw_Xlib_DataDestructor(Display *display, String class_name, XtPointer data)
{
  if (data)
    {
      XawXlibData *xdata = (XawXlibData *)data;

      XFreeGC(display, xdata->gc);
      if (xdata->dashes)
	XtFree(xdata->dashes);
      XtFree((char *)data);
    }
}

/* Start of DLInfo Management Functions */
static int
qcmp_dlist_info(register _Xconst void *left, register _Xconst void *right)
{
  return (strcmp((*(XawDLInfo **)left)->name, (*(XawDLInfo **)right)->name));
}

Bool XawDeclareDisplayListProc(XawDLClass *lc, String name,
				  XawDisplayListProc proc)
{
  XawDLInfo *info;

  if (!lc || !proc || !name || name[0] == '\0')
    return (False);

  if ((info = _XawFindDLInfo(lc, name)) != NULL)
    /* Since the data structures to the displayList classes are(should be)
     * opaque, it is not a good idea to allow overriding a displayList
     * procedure; it's better to choose another name or class name!
     */
    return (False);

  info = (XawDLInfo *)XtMalloc(sizeof(XawDLInfo));
  info->name = XtNewString(name);
  info->qname = XrmStringToQuark(info->name);
  info->proc = proc;

  if (!lc->num_infos)
    {
      lc->num_infos = 1;
      lc->infos = (XawDLInfo **)XtMalloc(sizeof(XawDLInfo*));
    }
  else
    {
      ++lc->num_infos;
      lc->infos = (XawDLInfo **)
	XtRealloc((char *)lc->infos, sizeof(XawDLInfo*) * lc->num_infos);
    }
  lc->infos[lc->num_infos - 1] = info;

  if (lc->num_infos > 1)
    qsort(lc->infos, lc->num_infos, sizeof(XawDLInfo*), qcmp_dlist_info);

  return (True);
}

static int
bcmp_dlist_info(register _Xconst void *string,
		register _Xconst void *dlinfo)
{
  return (strcmp((String)string, (*(XawDLClass **)dlinfo)->name));
}

static XawDLInfo *
_XawFindDLInfo(XawDLClass *lc, String name)
{
  XawDLInfo **info;

  if (!lc->infos)
    return (NULL);

  info = (XawDLInfo **)bsearch(name, lc->infos, lc->num_infos,
			       sizeof(XawDLInfo*), bcmp_dlist_info);

  return (info ? *info : NULL);
}

/* Start of DLClass Management Functions */
XawDLClass *
XawGetDisplayListClass(String name)
{
  return (_XawFindDLClass(name));
}

static int
qcmp_dlist_class(register _Xconst void *left, register _Xconst void *right)
{
  return (strcmp((*(XawDLClass **)left)->name, (*(XawDLClass **)right)->name));
}

XawDLClass *
XawCreateDisplayListClass(String name,
			  XawDLArgsInitProc args_init,
			  XawDLArgsDestructor args_destructor,
			  XawDLDataInitProc data_init,
			  XawDLDataDestructor data_destructor)
{
  XawDLClass *lc;

  if (!name || name[0] == '\0')
    return (NULL);

  lc = (XawDLClass *)XtMalloc(sizeof(XawDLClass));
  lc->name = XtNewString(name);
  lc->infos = NULL;
  lc->num_infos = 0;
  lc->args_init = args_init;
  lc->args_destructor = args_destructor;
  lc->data_init = data_init;
  lc->data_destructor = data_destructor;

  if (!classes)
    {
      num_classes = 1;
      classes = (XawDLClass **)XtMalloc(sizeof(XawDLClass));
    }
  else
    {
      ++num_classes;
      classes = (XawDLClass **)XtRealloc((char *)classes,
					 sizeof(XawDLClass) * num_classes);
    }
  classes[num_classes - 1] = lc;

  if (num_classes > 1)
    qsort(&classes[0], num_classes, sizeof(XawDLClass*), qcmp_dlist_class);

  return (lc);
}

static int
bcmp_dlist_class(register _Xconst void *string,
		 register _Xconst void *dlist)
{
  return (strcmp((String)string, (*(XawDLClass **)dlist)->name));
}

static XawDLClass *
_XawFindDLClass(String name)
{
  XawDLClass **lc;

  if (!classes)
    return (NULL);

  lc = (XawDLClass **)bsearch(name, &classes[0], num_classes,
			      sizeof(XawDLClass*), bcmp_dlist_class);

  return (lc ? *lc : NULL);
}

#endif /* OLDXAW */
