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
#include "IntrinsicI.h"
#include "Intrinsic.h"

/*
 * XtGetResourceList(), XtGetConstraintResourceList()
 */

#define TOXRMQUARK(p) ((XrmQuark)(long)(p))     /* avoid LP64 warnings */

void
XtGetResourceList(WidgetClass widget_class,
                  XtResourceList *resources,
                  Cardinal *num_resources)
{
    int size;
    register Cardinal i, dest = 0;
    register XtResourceList *list, dlist;

    LOCK_PROCESS;
    *resources = XtMallocArray(widget_class->core_class.num_resources,
                               (Cardinal) sizeof(XtResource));
    size = (int) (widget_class->core_class.num_resources * sizeof(XtResource));

    if (!widget_class->core_class.class_inited) {
        /* Easy case */

        (void) memcpy(*resources, widget_class->core_class.resources,
                      (size_t) size);
        *num_resources = widget_class->core_class.num_resources;
        UNLOCK_PROCESS;
        return;
    }

    /* Nope, it's the hard case */

    list = (XtResourceList *) widget_class->core_class.resources;
    dlist = *resources;
    for (i = 0; i < widget_class->core_class.num_resources; i++) {
        if (list[i] != NULL) {
            dlist[dest].resource_name = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->resource_name));
            dlist[dest].resource_class = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->resource_class));
            dlist[dest].resource_type = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->resource_type));
            dlist[dest].resource_size = list[i]->resource_size;
            /* trust that resource_offset isn't that big */
            dlist[dest].resource_offset = (Cardinal)
                -((int) (list[i]->resource_offset + 1));
            dlist[dest].default_type = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->default_type));
            dlist[dest].default_addr = list[i]->default_addr;
            dest++;
        }
    }
    *num_resources = dest;
    UNLOCK_PROCESS;
}

static Boolean
ClassIsSubclassOf(WidgetClass class, const WidgetClass superclass)
{
    for (; class != NULL; class = class->core_class.superclass) {
        if (class == superclass)
            return True;
    }
    return False;
}

void
XtGetConstraintResourceList(WidgetClass widget_class,
                            XtResourceList *resources,
                            Cardinal *num_resources)
{
    int size;
    register Cardinal i, dest = 0;
    register XtResourceList *list, dlist;
    ConstraintWidgetClass class = (ConstraintWidgetClass) widget_class;

    LOCK_PROCESS;
    if ((class->core_class.class_inited &&
         !(class->core_class.class_inited & ConstraintClassFlag))
        || (!class->core_class.class_inited &&
            !ClassIsSubclassOf(widget_class, constraintWidgetClass))
        || class->constraint_class.num_resources == 0) {

        *resources = NULL;
        *num_resources = 0;
        UNLOCK_PROCESS;
        return;
    }

    *resources = XtMallocArray(class->constraint_class.num_resources,
                               (Cardinal) sizeof(XtResource));
    size = (int) (class->constraint_class.num_resources * sizeof(XtResource));

    if (!class->core_class.class_inited) {
        /* Easy case */

        (void) memcpy(*resources, class->constraint_class.resources,
                      (size_t) size);
        *num_resources = class->constraint_class.num_resources;
        UNLOCK_PROCESS;
        return;
    }

    /* Nope, it's the hard case */

    list = (XtResourceList *) class->constraint_class.resources;
    dlist = *resources;
    for (i = 0; i < class->constraint_class.num_resources; i++) {
        if (list[i] != NULL) {
            dlist[dest].resource_name = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->resource_name));
            dlist[dest].resource_class = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->resource_class));
            dlist[dest].resource_type = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->resource_type));
            dlist[dest].resource_size = list[i]->resource_size;
            /* trust that resource_offset isn't that big */
            dlist[dest].resource_offset = (Cardinal)
                -((int) (list[i]->resource_offset + 1));
            dlist[dest].default_type = (String)
                XrmQuarkToString(TOXRMQUARK(list[i]->default_type));
            dlist[dest].default_addr = list[i]->default_addr;
            dest++;
        }
    }
    *num_resources = dest;
    UNLOCK_PROCESS;
}
