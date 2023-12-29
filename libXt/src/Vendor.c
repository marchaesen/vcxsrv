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

/* Make sure all wm properties can make it out of the resource manager */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "StringDefs.h"
#include "Shell.h"
#include "ShellP.h"
#include "Vendor.h"
#include "VendorP.h"
#include <stdio.h>

/***************************************************************************
 *
 * Vendor shell class record
 *
 ***************************************************************************/

#if defined(__CYGWIN__) || defined(__MINGW32__)
/* to fix the EditRes problem because of wrong linker semantics */
extern WidgetClass vendorShellWidgetClass;

int __stdcall
DllMain(unsigned long mod_handle, unsigned long flag, void *routine)
{
    switch (flag) {
    case 1:                    /* DLL_PROCESS_ATTACH - process attach */
        vendorShellWidgetClass = (WidgetClass) (&vendorShellClassRec);
        break;
    case 0:                    /* DLL_PROCESS_DETACH - process detach */
        break;
    }
    return 1;
}
#endif

/* *INDENT-OFF* */
externaldef(vendorshellclassrec)
VendorShellClassRec vendorShellClassRec = {
    {
     /* superclass            */ (WidgetClass) &wmShellClassRec,
     /* class_name            */ "VendorShell",
     /* size                  */ sizeof(VendorShellRec),
     /* Class Initializer     */ NULL,
     /* class_part_initialize */ NULL,
     /* Class init'ed ?       */ FALSE,
     /* initialize            */ NULL,
     /* initialize_notify     */ NULL,
     /* realize               */ XtInheritRealize,
     /* actions               */ NULL,
     /* num_actions           */ 0,
     /* resources             */ NULL,
     /* resource_count        */ 0,
     /* xrm_class             */ NULLQUARK,
     /* compress_motion       */ FALSE,
     /* compress_exposure     */ TRUE,
     /* compress_enterleave   */ FALSE,
     /* visible_interest      */ FALSE,
     /* destroy               */ NULL,
     /* resize                */ XtInheritResize,
     /* expose                */ NULL,
     /* set_values            */ NULL,
     /* set_values_hook       */ NULL,
     /* set_values_almost     */ XtInheritSetValuesAlmost,
     /* get_values_hook       */ NULL,
     /* accept_focus          */ NULL,
     /* intrinsics version    */ XtVersion,
     /* callback offsets      */ NULL,
     /* tm_table              */ NULL,
     /* query_geometry        */ NULL,
     /* display_accelerator   */ NULL,
     /* extension             */ NULL
     },
     {
       /* geometry_manager    */ XtInheritGeometryManager,
       /* change_managed      */ XtInheritChangeManaged,
       /* insert_child        */ XtInheritInsertChild,
       /* delete_child        */ XtInheritDeleteChild,
       /* extension           */ NULL
     },
     {
       /* extension           */ NULL
     },
     {
       /* extension           */ NULL
     },
     {
       /* extension           */ NULL
     }
};
/* *INDENT-ON* */

#if !defined(AIXSHLIB) || !defined(SHAREDCODE)
externaldef(vendorshellwidgetclass)
WidgetClass vendorShellWidgetClass = (WidgetClass) (&vendorShellClassRec);
#endif
