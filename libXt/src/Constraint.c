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
#include "StringDefs.h"

static void ConstraintPartInitialize(WidgetClass wc);

externaldef(constraintclassrec)
ConstraintClassRec constraintClassRec = {
    {
    /******* CorePart *******/
     /* superclass           */ (WidgetClass) &compositeClassRec,
     /* class_name           */ "Constraint",
     /* widget_size          */ sizeof(ConstraintRec),
     /* class_initialize     */ NULL,
     /* class_part_initialize */ ConstraintPartInitialize,
     /* class_inited         */ FALSE,
     /* initialize           */ NULL,
     /* initialize_hook      */ NULL,
     /* realize              */ XtInheritRealize,
     /* actions              */ NULL,
     /* num_actions          */ 0,
     /* resources            */ NULL,
     /* num_resources        */ 0,
     /* xrm_class            */ NULLQUARK,
     /* compress_motion      */ FALSE,
     /* compress_exposure    */ TRUE,
     /* compress_enterleave  */ FALSE,
     /* visible_interest     */ FALSE,
     /* destroy              */ NULL,
     /* resize               */ NULL,
     /* expose               */ NULL,
     /* set_values           */ NULL,
     /* set_values_hook      */ NULL,
     /* set_values_almost    */ XtInheritSetValuesAlmost,
     /* get_values_hook      */ NULL,
     /* accept_focus         */ NULL,
     /* version              */ XtVersion,
     /* callback_offsets     */ NULL,
     /* tm_table             */ NULL,
     /* query_geometry       */ NULL,
     /* display_accelerator  */ NULL,
     /* extension            */ NULL
     }
    , {
      /**** CompositePart *****/
       /* geometry_handler     */ NULL,
       /* change_managed       */ NULL,
       /* insert_child         */ XtInheritInsertChild,
       /* delete_child         */ XtInheritDeleteChild,
       /* extension            */ NULL
       }
    , {
      /**** ConstraintPart ****/
       /* resources            */ NULL,
       /* num_resources        */ 0,
       /* constraint_size      */ 0,
       /* initialize           */ NULL,
       /* destroy              */ NULL,
       /* set_values           */ NULL,
       /* extension            */ NULL
       }
};

externaldef(constraintwidgetclass)
WidgetClass constraintWidgetClass = (WidgetClass) &constraintClassRec;

static void
ConstraintPartInitialize(WidgetClass wc)
{
    ConstraintWidgetClass cwc = (ConstraintWidgetClass) wc;

    if (cwc->constraint_class.resources)
        _XtCompileResourceList(cwc->constraint_class.resources,
                               cwc->constraint_class.num_resources);

    _XtConstraintResDependencies((ConstraintWidgetClass) wc);
}
