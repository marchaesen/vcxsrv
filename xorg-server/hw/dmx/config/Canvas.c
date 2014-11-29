/*
 * Copyright 1987, 1998  The Open Group
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of The Open Group shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from The Open Group.
 */

/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 * This file was originally taken from xc/lib/Xaw/Template.c
 */

#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include "CanvasP.h"

static void
CanvasInitialize(Widget request, Widget w, ArgList args, Cardinal * num_args)
{
}

static void
CanvasExpose(Widget w, XEvent * event, Region region)
{
    CanvasExposeDataRec data;

    data.w = w;
    data.event = event;
    data.region = region;

    if (!XtIsRealized(w))
        return;
    XtCallCallbacks(w, XtNcanvasExposeCallback, (XtPointer) &data);
}

static void
CanvasResize(Widget w)
{
    if (!XtIsRealized(w))
        return;
    XtCallCallbacks(w, XtNcanvasResizeCallback, (XtPointer) w);
}

static void
CanvasAction(Widget w, XEvent * event, String * params, Cardinal * num_params)
{
    XtCallCallbacks(w, XtNcallback, (XtPointer) event);
}

#define offset(field) XtOffsetOf(CanvasRec, canvas.field)
static XtResource resources[] = {
    {XtNcallback, XtCCallback, XtRCallback,
     sizeof(XtCallbackList), offset(input_callback), XtRCallback, NULL}
    ,
    {(char *) XtNcanvasExposeCallback, (char *) XtCcanvasExposeCallback, XtRCallback,
     sizeof(XtCallbackList), offset(expose_callback), XtRCallback, NULL}
    ,
    {(char *) XtNcanvasResizeCallback, (char *) XtCcanvasResizeCallback, XtRCallback,
     sizeof(XtCallbackList), offset(resize_callback), XtRCallback, NULL}
    ,
};

#undef offset

static XtActionsRec actions[] = {
    {(char *) "canvas", CanvasAction},
};

static char translations[] = "<Key>:    canvas()\n\
<Motion>:  canvas()\n\
<BtnDown>: canvas()\n\
<BtnUp>: canvas()\n\
";

#define Superclass	(&widgetClassRec)
CanvasClassRec canvasClassRec = {
    /* core */
    {
     (WidgetClass) Superclass,  /* superclass */
     (char *) "Canvas",         /* class_name */
     sizeof(CanvasRec),         /* widget_size */
     NULL,                      /* class_initialize */
     NULL,                      /* class_part_initialize */
     False,                     /* class_inited */
     CanvasInitialize,          /* initialize */
     NULL,                      /* initialize_hook */
     XtInheritRealize,          /* realize */
     actions,                   /* actions */
     XtNumber(actions),         /* num_actions */
     resources,                 /* resources */
     XtNumber(resources),       /* num_resources */
     NULLQUARK,                 /* xrm_class */
     True,                      /* compress_motion */
     True,                      /* compress_exposure */
     True,                      /* compress_enterleave */
     False,                     /* visible_interest */
     NULL,                      /* destroy */
     CanvasResize,              /* resize */
     CanvasExpose,              /* expose */
     NULL,                      /* set_values */
     NULL,                      /* set_values_hook */
     XtInheritSetValuesAlmost,  /* set_values_almost */
     NULL,                      /* get_values_hook */
     NULL,                      /* accept_focus */
     XtVersion,                 /* version */
     NULL,                      /* callback_private */
     translations,              /* tm_table */
     XtInheritQueryGeometry,    /* query_geometry */
     XtInheritDisplayAccelerator,       /* display_accelerator */
     NULL,                      /* extension */
     }
    ,
    /* canvas */
    {
     NULL,                      /* extension */
     }
};

WidgetClass canvasWidgetClass = (WidgetClass) &canvasClassRec;
