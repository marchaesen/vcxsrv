/***********************************************************
Copyright (c) 1993, Oracle and/or its affiliates.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

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

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicP.h"
#include "EventI.h"
#include "ConvertI.h"
#include "TranslateI.h"
#include "ResourceI.h"
#include "RectObj.h"
#include "RectObjP.h"
#include "ThreadsI.h"
#include "StringDefs.h"

/******************************************************************
 *
 * CoreWidget Resources
 *
 ******************************************************************/

externaldef(xtinherittranslations)
int _XtInheritTranslations = 0;
extern String XtCXtToolkitError;        /* from IntrinsicI.h */
static void
XtCopyScreen(Widget, int, XrmValue *);

static XtResource resources[] = {
    {XtNscreen, XtCScreen, XtRScreen, sizeof(Screen *),
     XtOffsetOf(CoreRec, core.screen), XtRCallProc, (XtPointer) XtCopyScreen},
/*_XtCopyFromParent does not work for screen because the Display
parameter is not passed through to the XtRCallProc routines */
    {XtNdepth, XtCDepth, XtRInt, sizeof(int),
     XtOffsetOf(CoreRec, core.depth),
     XtRCallProc, (XtPointer) _XtCopyFromParent},
    {XtNcolormap, XtCColormap, XtRColormap, sizeof(Colormap),
     XtOffsetOf(CoreRec, core.colormap),
     XtRCallProc, (XtPointer) _XtCopyFromParent},
    {XtNbackground, XtCBackground, XtRPixel, sizeof(Pixel),
     XtOffsetOf(CoreRec, core.background_pixel),
     XtRString, (XtPointer) "XtDefaultBackground"},
    {XtNbackgroundPixmap, XtCPixmap, XtRPixmap, sizeof(Pixmap),
     XtOffsetOf(CoreRec, core.background_pixmap),
     XtRImmediate, (XtPointer) XtUnspecifiedPixmap},
    {XtNborderColor, XtCBorderColor, XtRPixel, sizeof(Pixel),
     XtOffsetOf(CoreRec, core.border_pixel),
     XtRString, (XtPointer) "XtDefaultForeground"},
    {XtNborderPixmap, XtCPixmap, XtRPixmap, sizeof(Pixmap),
     XtOffsetOf(CoreRec, core.border_pixmap),
     XtRImmediate, (XtPointer) XtUnspecifiedPixmap},
    {XtNmappedWhenManaged, XtCMappedWhenManaged, XtRBoolean, sizeof(Boolean),
     XtOffsetOf(CoreRec, core.mapped_when_managed),
     XtRImmediate, (XtPointer) True},
    {XtNtranslations, XtCTranslations, XtRTranslationTable,
     sizeof(XtTranslations), XtOffsetOf(CoreRec, core.tm.translations),
     XtRTranslationTable, (XtPointer) NULL},
    {XtNaccelerators, XtCAccelerators, XtRAcceleratorTable,
     sizeof(XtTranslations), XtOffsetOf(CoreRec, core.accelerators),
     XtRTranslationTable, (XtPointer) NULL}
};

static void CoreInitialize(Widget, Widget, ArgList, Cardinal *);
static void CoreClassPartInitialize(WidgetClass);
static void CoreDestroy(Widget);
static void CoreRealize(Widget, XtValueMask *, XSetWindowAttributes *);
static Boolean CoreSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static void CoreSetValuesAlmost(Widget, Widget, XtWidgetGeometry *,
                                XtWidgetGeometry *);

static RectObjClassRec unNamedObjClassRec = {
    {
     /* superclass         */ (WidgetClass) &rectObjClassRec,
     /* class_name         */ "UnNamedObj",
     /* widget_size        */ 0,
     /* class_initialize   */ NULL,
     /* class_part_initialize */ NULL,
     /* class_inited       */ FALSE,
     /* initialize         */ NULL,
     /* initialize_hook    */ NULL,
     /* realize            */ (XtProc) XtInheritRealize,
     /* actions            */ NULL,
     /* num_actions        */ 0,
     /* resources          */ NULL,
     /* num_resources      */ 0,
     /* xrm_class          */ NULLQUARK,
     /* compress_motion    */ FALSE,
     /* compress_exposure  */ FALSE,
     /* compress_enterleave */ FALSE,
     /* visible_interest   */ FALSE,
     /* destroy            */ NULL,
     /* resize             */ NULL,
     /* expose             */ NULL,
     /* set_values         */ NULL,
     /* set_values_hook    */ NULL,
     /* set_values_almost  */ XtInheritSetValuesAlmost,
     /* get_values_hook    */ NULL,
     /* accept_focus       */ NULL,
     /* version            */ XtVersion,
     /* callback_offsets   */ NULL,
     /* tm_table           */ NULL,
     /* query_geometry       */ NULL,
     /* display_accelerator  */ NULL,
     /* extension            */ NULL
     }
};

externaldef(widgetclassrec)
WidgetClassRec widgetClassRec = {
    {
     /* superclass         */ (WidgetClass) &unNamedObjClassRec,
     /* class_name         */ "Core",
     /* widget_size        */ sizeof(WidgetRec),
     /* class_initialize   */ NULL,
     /* class_part_initialize */ CoreClassPartInitialize,
     /* class_inited       */ FALSE,
     /* initialize         */ CoreInitialize,
     /* initialize_hook    */ NULL,
     /* realize            */ CoreRealize,
     /* actions            */ NULL,
     /* num_actions        */ 0,
     /* resources          */ resources,
     /* num_resources      */ XtNumber(resources),
     /* xrm_class          */ NULLQUARK,
     /* compress_motion    */ FALSE,
     /* compress_exposure  */ TRUE,
     /* compress_enterleave */ FALSE,
     /* visible_interest   */ FALSE,
     /* destroy            */ CoreDestroy,
     /* resize             */ NULL,
     /* expose             */ NULL,
     /* set_values         */ CoreSetValues,
     /* set_values_hook    */ NULL,
     /* set_values_almost  */ CoreSetValuesAlmost,
     /* get_values_hook    */ NULL,
     /* accept_focus       */ NULL,
     /* version            */ XtVersion,
     /* callback_offsets   */ NULL,
     /* tm_table           */ NULL,
     /* query_geometry       */ NULL,
     /* display_accelerator  */ NULL,
     /* extension            */ NULL
     }
};

externaldef(WidgetClass)
WidgetClass widgetClass = &widgetClassRec;

externaldef(WidgetClass)
WidgetClass coreWidgetClass = &widgetClassRec;

static void
XtCopyScreen(Widget widget, int offset _X_UNUSED, XrmValue *value)
{
    value->addr = (XPointer) (&widget->core.screen);
}

/*
 * Start of Core methods
 */

static void
CoreClassPartInitialize(register WidgetClass wc)
{
    /* We don't need to check for null super since we'll get to object
       eventually, and it had better define them!  */

    register WidgetClass super = wc->core_class.superclass;

    LOCK_PROCESS;
    if (wc->core_class.realize == XtInheritRealize) {
        wc->core_class.realize = super->core_class.realize;
    }

    if (wc->core_class.accept_focus == XtInheritAcceptFocus) {
        wc->core_class.accept_focus = super->core_class.accept_focus;
    }

    if (wc->core_class.display_accelerator == XtInheritDisplayAccelerator) {
        wc->core_class.display_accelerator =
            super->core_class.display_accelerator;
    }

    if (wc->core_class.tm_table == XtInheritTranslations) {
        wc->core_class.tm_table =
            wc->core_class.superclass->core_class.tm_table;
    }
    else if (wc->core_class.tm_table != NULL) {
        wc->core_class.tm_table =
            (String) XtParseTranslationTable(wc->core_class.tm_table);
    }

    if (wc->core_class.actions != NULL) {
        Boolean inPlace;

        if (wc->core_class.version == XtVersionDontCheck)
            inPlace = True;
        else
            inPlace = (wc->core_class.version < XtVersion) ? False : True;

        /* Compile the action table into a more efficient form */
        wc->core_class.actions =
            (XtActionList) _XtInitializeActionData(wc->core_class.actions,
                                                   wc->core_class.num_actions,
                                                   inPlace);
    }
    UNLOCK_PROCESS;
}

static void
CoreInitialize(Widget requested_widget _X_UNUSED,
               register Widget new_widget,
               ArgList args _X_UNUSED,
               Cardinal *num_args _X_UNUSED)
{
    XtTranslations save1, save2;

    new_widget->core.event_table = NULL;
    new_widget->core.tm.proc_table = NULL;
    new_widget->core.tm.lastEventTime = 0;
    /* magic semi-resource fetched by GetResources */
    save1 = (XtTranslations) new_widget->core.tm.current_state;
    new_widget->core.tm.current_state = NULL;
    save2 = new_widget->core.tm.translations;
    LOCK_PROCESS;
    new_widget->core.tm.translations =
        (XtTranslations) new_widget->core.widget_class->core_class.tm_table;
    UNLOCK_PROCESS;
    if (save1)
        _XtMergeTranslations(new_widget, save1, save1->operation);
    if (save2)
        _XtMergeTranslations(new_widget, save2, save2->operation);
}

static void
CoreRealize(Widget widget,
            XtValueMask *value_mask,
            XSetWindowAttributes *attributes)
{
    XtCreateWindow(widget, (unsigned int) InputOutput,
                   (Visual *) CopyFromParent, *value_mask, attributes);
}                               /* CoreRealize */

static void
CoreDestroy(Widget widget)
{
    _XtFreeEventTable(&widget->core.event_table);
    _XtDestroyTMData(widget);
    XtUnregisterDrawable(XtDisplay(widget), widget->core.window);

    if (widget->core.popup_list != NULL)
        XtFree((char *) widget->core.popup_list);

}                               /* CoreDestroy */

static Boolean
CoreSetValues(Widget old,
              Widget reference _X_UNUSED,
              Widget new,
              ArgList args _X_UNUSED,
              Cardinal *num_args _X_UNUSED)
{
    Boolean redisplay;
    Mask window_mask;
    XSetWindowAttributes attributes;

    redisplay = FALSE;
    if (old->core.tm.translations != new->core.tm.translations) {
        XtTranslations save = new->core.tm.translations;

        new->core.tm.translations = old->core.tm.translations;
        _XtMergeTranslations(new, save, XtTableReplace);
    }

    /* Check everything that depends upon window being realized */
    if (XtIsRealized(old)) {
        window_mask = 0;
        /* Check window attributes */
        if (old->core.background_pixel != new->core.background_pixel
            && new->core.background_pixmap == XtUnspecifiedPixmap) {
            attributes.background_pixel = new->core.background_pixel;
            window_mask |= CWBackPixel;
            redisplay = TRUE;
        }
        if (old->core.background_pixmap != new->core.background_pixmap) {
            if (new->core.background_pixmap == XtUnspecifiedPixmap) {
                window_mask |= CWBackPixel;
                attributes.background_pixel = new->core.background_pixel;
            }
            else {
                attributes.background_pixmap = new->core.background_pixmap;
                window_mask &= (unsigned long) (~CWBackPixel);
                window_mask |= CWBackPixmap;
            }
            redisplay = TRUE;
        }
        if (old->core.border_pixel != new->core.border_pixel
            && new->core.border_pixmap == XtUnspecifiedPixmap) {
            attributes.border_pixel = new->core.border_pixel;
            window_mask |= CWBorderPixel;
        }
        if (old->core.border_pixmap != new->core.border_pixmap) {
            if (new->core.border_pixmap == XtUnspecifiedPixmap) {
                window_mask |= CWBorderPixel;
                attributes.border_pixel = new->core.border_pixel;
            }
            else {
                attributes.border_pixmap = new->core.border_pixmap;
                window_mask &= (unsigned long) (~CWBorderPixel);
                window_mask |= CWBorderPixmap;
            }
        }
        if (old->core.depth != new->core.depth) {
            XtAppWarningMsg(XtWidgetToApplicationContext(old),
                            "invalidDepth", "setValues", XtCXtToolkitError,
                            "Can't change widget depth", NULL, NULL);
            new->core.depth = old->core.depth;
        }
        if (old->core.colormap != new->core.colormap) {
            window_mask |= CWColormap;
            attributes.colormap = new->core.colormap;
        }
        if (window_mask != 0) {
            /* Actually change X window attributes */
            XChangeWindowAttributes(XtDisplay(new), XtWindow(new), window_mask,
                                    &attributes);
        }

        if (old->core.mapped_when_managed != new->core.mapped_when_managed) {
            Boolean mapped_when_managed = new->core.mapped_when_managed;

            new->core.mapped_when_managed = !mapped_when_managed;
            XtSetMappedWhenManaged(new, mapped_when_managed);
        }
    }                           /* if realized */

    return redisplay;
}                               /* CoreSetValues */

static void
CoreSetValuesAlmost(Widget old _X_UNUSED,
                    Widget new _X_UNUSED,
                    XtWidgetGeometry *request,
                    XtWidgetGeometry *reply)
{
    *request = *reply;
}
