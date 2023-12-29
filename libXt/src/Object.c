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

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "StringDefs.h"

/* *INDENT-OFF* */
static XtResource resources[] = {
    {XtNdestroyCallback, XtCCallback, XtRCallback,sizeof(XtPointer),
     XtOffsetOf(ObjectRec,object.destroy_callbacks),
     XtRCallback, (XtPointer)NULL}
};
/* *INDENT-ON* */

static void ObjectClassPartInitialize(WidgetClass);
static Boolean ObjectSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static void ObjectDestroy(Widget);

/* *INDENT-OFF* */
externaldef(objectclassrec) ObjectClassRec objectClassRec = {
  {
    /* superclass              */ NULL,
    /* class_name              */ "Object",
    /* widget_size             */ sizeof(ObjectRec),
    /* class_initialize        */ NULL,
    /* class_part_initialize   */ ObjectClassPartInitialize,
    /* class_inited            */ FALSE,
    /* initialize              */ NULL,
    /* initialize_hook         */ NULL,
    /* pad                     */ NULL,
    /* pad                     */ NULL,
    /* pad                     */ 0,
    /* resources               */ resources,
    /* num_resources           */ XtNumber(resources),
    /* xrm_class               */ NULLQUARK,
    /* pad                     */ FALSE,
    /* pad                     */ FALSE,
    /* pad                     */ FALSE,
    /* pad                     */ FALSE,
    /* destroy                 */ ObjectDestroy,
    /* pad                     */ NULL,
    /* pad                     */ NULL,
    /* set_values              */ ObjectSetValues,
    /* set_values_hook         */ NULL,
    /* pad                     */ NULL,
    /* get_values_hook         */ NULL,
    /* pad                     */ NULL,
    /* version                 */ XtVersion,
    /* callback_offsets        */ NULL,
    /* pad                     */ NULL,
    /* pad                     */ NULL,
    /* pad                     */ NULL,
    /* extension               */ NULL
  }
};
/* *INDENT-ON* */

externaldef(objectClass)
WidgetClass objectClass = (WidgetClass) &objectClassRec;

/*
 * Start of object routines.
 */

static void
ConstructCallbackOffsets(WidgetClass myWidgetClass)
{
    static XrmQuark QCallback = NULLQUARK;
    register int i;
    register int tableSize;
    register CallbackTable newTable;
    register CallbackTable superTable;
    register XrmResourceList resourceList;
    ObjectClass myObjectClass = (ObjectClass) myWidgetClass;

    /*
       This function builds an array of pointers to the resource
       structures which describe the callbacks for this widget class.
       This array is special in that the 0th entry is the number of
       callback pointers.
     */

    if (QCallback == NULLQUARK)
        QCallback = XrmPermStringToQuark(XtRCallback);

    if (myObjectClass->object_class.superclass != NULL) {
        superTable = (CallbackTable)
            ((ObjectClass) myObjectClass->object_class.
             superclass)->object_class.callback_private;
        tableSize = (int) (long) superTable[0];
    }
    else {
        superTable = (CallbackTable) NULL;
        tableSize = 0;
    }

    /* Count the number of callbacks */
    resourceList = (XrmResourceList) myObjectClass->object_class.resources;
    if (resourceList != NULL) {
        for (i = (int) myObjectClass->object_class.num_resources; --i >= 0;
             resourceList++) {
            if (resourceList->xrm_type == QCallback)
                tableSize++;
        }
    }

    /*
     * Allocate and load the table.  Make sure that the new callback
     * offsets occur in the table ahead of the superclass callback
     * offsets so that resource overrides work.
     */
    newTable = XtMallocArray((Cardinal) tableSize + 1,
                             (Cardinal) sizeof(XrmResource *));

    newTable[0] = (XrmResource *) (XtIntPtr) tableSize;

    if (superTable)
        tableSize -= (int) (long) superTable[0];
    resourceList = (XrmResourceList) myObjectClass->object_class.resources;
    for (i = 1; tableSize > 0; resourceList++)
        if (resourceList->xrm_type == QCallback) {
            newTable[i++] = resourceList;
            tableSize--;
        }

    if (superTable)
        for (tableSize = (int) (long) *superTable++;
             --tableSize >= 0; superTable++)
            newTable[i++] = *superTable;

    myObjectClass->object_class.callback_private = (XtPointer) newTable;
}

static void
InheritObjectExtensionMethods(WidgetClass widget_class)
{
    ObjectClass oc = (ObjectClass) widget_class;
    ObjectClassExtension ext, super_ext = NULL;

    ext = (ObjectClassExtension)
        XtGetClassExtension(widget_class,
                            XtOffsetOf(ObjectClassRec, object_class.extension),
                            NULLQUARK, XtObjectExtensionVersion,
                            sizeof(ObjectClassExtensionRec));

    if (oc->object_class.superclass)
        super_ext = (ObjectClassExtension)
            XtGetClassExtension(oc->object_class.superclass,
                                XtOffsetOf(ObjectClassRec,
                                           object_class.extension), NULLQUARK,
                                XtObjectExtensionVersion,
                                sizeof(ObjectClassExtensionRec));
    LOCK_PROCESS;
    if (ext) {
        if (ext->allocate == XtInheritAllocate)
            ext->allocate = (super_ext ? super_ext->allocate : NULL);
        if (ext->deallocate == XtInheritDeallocate)
            ext->deallocate = (super_ext ? super_ext->deallocate : NULL);
    }
    else if (super_ext) {
        /* Be careful to inherit only what is appropriate */
        ext = (ObjectClassExtension)
            __XtCalloc(1, sizeof(ObjectClassExtensionRec));
        ext->next_extension = oc->object_class.extension;
        ext->record_type = NULLQUARK;
        ext->version = XtObjectExtensionVersion;
        ext->record_size = sizeof(ObjectClassExtensionRec);
        ext->allocate = super_ext->allocate;
        ext->deallocate = super_ext->deallocate;
        oc->object_class.extension = (XtPointer) ext;
    }
    UNLOCK_PROCESS;
}

static void
ObjectClassPartInitialize(register WidgetClass wc)
{
    ObjectClass oc = (ObjectClass) wc;

    oc->object_class.xrm_class =
        XrmPermStringToQuark(oc->object_class.class_name);

    if (oc->object_class.resources)
        _XtCompileResourceList(oc->object_class.resources,
                               oc->object_class.num_resources);

    ConstructCallbackOffsets(wc);
    _XtResourceDependencies(wc);
    InheritObjectExtensionMethods(wc);
}

static Boolean
ObjectSetValues(Widget old,
                Widget request _X_UNUSED,
                Widget widget,
                ArgList args _X_UNUSED,
                Cardinal *num_args _X_UNUSED)
{
    CallbackTable offsets;
    int i;

    LOCK_PROCESS;
    /* Compile any callback lists into internal form */
    offsets = (CallbackTable) XtClass(widget)->core_class.callback_private;

    for (i = (int) (long) *(offsets++); --i >= 0; offsets++) {
        InternalCallbackList *ol, *nl;

        ol = (InternalCallbackList *)
            ((char *) old - (*offsets)->xrm_offset - 1);
        nl = (InternalCallbackList *)
            ((char *) widget - (*offsets)->xrm_offset - 1);
        if (*ol != *nl) {
            if (*ol != NULL)
                XtFree((char *) *ol);
            if (*nl != NULL)
                *nl = _XtCompileCallbackList((XtCallbackList) *nl);
        }
    }
    UNLOCK_PROCESS;
    return False;
}

static void
ObjectDestroy(register Widget widget)
{
    CallbackTable offsets;
    int i;

    /* Remove all callbacks associated with widget */
    LOCK_PROCESS;
    offsets = (CallbackTable)
        widget->core.widget_class->core_class.callback_private;

    for (i = (int) (long) *(offsets++); --i >= 0; offsets++) {
        InternalCallbackList cl = *(InternalCallbackList *)
            ((char *) widget - (*offsets)->xrm_offset - 1);

        if (cl)
            XtFree((char *) cl);
    }
    UNLOCK_PROCESS;
}                               /* ObjectDestroy */
