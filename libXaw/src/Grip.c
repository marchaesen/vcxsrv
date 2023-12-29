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
 * Grip.c - Grip Widget (Used by Paned Widget)
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/GripP.h>
#include <X11/Xaw/XawInit.h>

/*
 * Prototypes
 */
static void
GripAction(Widget, XEvent*, String*, Cardinal*);

/*
 * Initialization
 */
static XtResource resources[] = {
  {
    XtNwidth,
    XtCWidth,
    XtRDimension,
    sizeof(Dimension),
    XtOffsetOf(GripRec, core.width),
    XtRImmediate,
    (XtPointer)DEFAULT_GRIP_SIZE
  },
  {
    XtNheight,
    XtCHeight,
    XtRDimension,
    sizeof(Dimension),
    XtOffsetOf(GripRec, core.height),
    XtRImmediate,
    (XtPointer)DEFAULT_GRIP_SIZE
  },
  {
    XtNforeground,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    XtOffsetOf(GripRec, core.background_pixel),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNborderWidth,
    XtCBorderWidth,
    XtRDimension,
    sizeof(Dimension),
    XtOffsetOf(GripRec, core.border_width),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNcallback,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    XtOffsetOf(GripRec, grip.grip_action),
    XtRCallback,
    NULL
  },
};

static XtActionsRec actionsList[] =
{
  {"GripAction",      GripAction},
};

#define Superclass	(&simpleClassRec)

GripClassRec gripClassRec = {
  /* core */
   {
     (WidgetClass)Superclass,		/* superclass */
     "Grip",				/* class name */
     sizeof(GripRec),			/* size */
     XawInitializeWidgetSet,		/* class initialize */
     NULL,				/* class_part_init */
     False,				/* class_inited */
     NULL,				/* initialize */
     NULL,				/* initialize_hook */
     XtInheritRealize,			/* realize */
     actionsList,			/* actions */
     XtNumber(actionsList),		/* num_actions */
     resources,				/* resources */
     XtNumber(resources),		/* num_resources */
     NULLQUARK,				/* xrm_class */
     True,				/* compress_motion */
     True,				/* compress_exposure */
     True,				/* compress_enterleave */
     False,				/* visible_interest */
     NULL,				/* destroy */
     NULL,				/* resize */
     XtInheritExpose,			/* expose */
     NULL,				/* set_values */
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
     XtInheritChangeSensitive,		/* change_sensitive */
   },
   /* grip */
   {
     NULL,				/* extension */
   }
};

WidgetClass gripWidgetClass = (WidgetClass)&gripClassRec;

/*
 * Implementation
 */
static void
GripAction(Widget widget, XEvent *event, String *params, Cardinal *num_params)
{
    XawGripCallDataRec call_data;

    call_data.event = event;
    call_data.params = params;
    call_data.num_params = *num_params;

    XtCallCallbacks(widget, XtNcallback, (XtPointer)&call_data);
}
