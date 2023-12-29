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
 * This is a copy of Xt/Vendor.c with an additional ClassInitialize
 * procedure to register Xmu resource type converters, and all the
 * monkey business associated with input methods...
 *
 */

/* Make sure all wm properties can make it out of the resource manager */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/ShellP.h>
#include <X11/VendorP.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/Atoms.h>
#include <X11/Xmu/Editres.h>
#include <X11/Xmu/ExtAgent.h>

/* The following two headers are for the input method. */

#include <X11/Xaw/VendorEP.h>
#include <X11/Xaw/XawImP.h>

/*
 * Class Methods
 */
static void XawVendorShellChangeManaged(Widget);
static Boolean XawCvtCompoundTextToString(Display*, XrmValuePtr, Cardinal*,
					  XrmValue*, XrmValue*, XtPointer*);
static void XawVendorShellClassInitialize(void);
static XtGeometryResult XawVendorShellGeometryManager(Widget,
						      XtWidgetGeometry*,
						      XtWidgetGeometry*);
static void XawVendorShellExtClassInitialize(void);
static void XawVendorShellExtDestroy(Widget);
static void XawVendorShellExtInitialize(Widget, Widget, ArgList, Cardinal*);
void XawVendorShellExtResize(Widget);
static Boolean XawVendorShellExtSetValues(Widget, Widget, Widget,
					  ArgList, Cardinal*);
static void XawVendorShellClassPartInit(WidgetClass);
static void XawVendorShellInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawVendorShellRealize(Widget, Mask*, XSetWindowAttributes*);
static Boolean XawVendorShellSetValues(Widget, Widget, Widget,
				       ArgList, Cardinal*);

/*
 * External
 */
void XawVendorStructureNotifyHandler(Widget, XtPointer, XEvent*, Boolean*);

static XtResource resources[] = {
  {XtNinput, XtCInput, XtRBool, sizeof(Bool),
		XtOffsetOf(VendorShellRec, wm.wm_hints.input),
		XtRImmediate, (XtPointer)True}
};

/***************************************************************************
 *
 * Vendor shell class record
 *
 ***************************************************************************/

#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(__APPLE__)
/* to fix the EditRes problem because of wrong linker semantics */
extern WidgetClass vendorShellWidgetClass; /* from Xt/Vendor.c */
extern VendorShellClassRec _XawVendorShellClassRec;
extern void _XawFixupVendorShell();

#if defined(__APPLE__)
__attribute__((constructor))
static void __VendorShellHack(void)
{
    vendorShellWidgetClass = (WidgetClass)(&_XawVendorShellClassRec);
    _XawFixupVendorShell();
}
#else
int __stdcall
DllMain(unsigned long mod_handle, unsigned long flag, void *routine)
{
  switch (flag)
    {
    case 1: /* DLL_PROCESS_ATTACH - process attach */
      vendorShellWidgetClass = (WidgetClass)(&_XawVendorShellClassRec);
      _XawFixupVendorShell();
      break;
    case 0: /* DLL_PROCESS_DETACH - process detach */
      break;
    }
  return 1;
}
#endif

#define vendorShellClassRec _XawVendorShellClassRec

#endif

static CompositeClassExtensionRec vendorCompositeExt = {
    /* next_extension     */	NULL,
    /* record_type        */    NULLQUARK,
    /* version            */    XtCompositeExtensionVersion,
    /* record_size        */    sizeof (CompositeClassExtensionRec),
    /* accepts_objects    */    TRUE,
    /* allows_change_managed_set */ FALSE
};

#define SuperClass (&wmShellClassRec)
externaldef(vendorshellclassrec) VendorShellClassRec vendorShellClassRec = {
  {
    /* superclass	  */	(WidgetClass)SuperClass,
    /* class_name	  */	"VendorShell",
    /* size		  */	sizeof(VendorShellRec),
    /* class_initialize	  */	XawVendorShellClassInitialize,
    /* class_part_init	  */	XawVendorShellClassPartInit,
    /* Class init'ed ?	  */	FALSE,
    /* initialize         */	XawVendorShellInitialize,
    /* initialize_hook	  */	NULL,
    /* realize		  */	XawVendorShellRealize,
    /* actions		  */	NULL,
    /* num_actions	  */	0,
    /* resources	  */	resources,
    /* resource_count	  */	XtNumber(resources),
    /* xrm_class	  */	NULLQUARK,
    /* compress_motion	  */	FALSE,
    /* compress_exposure  */	TRUE,
    /* compress_enterleave*/	FALSE,
    /* visible_interest	  */	FALSE,
    /* destroy		  */	NULL,
    /* resize		  */	XawVendorShellExtResize,
    /* expose		  */	NULL,
    /* set_values	  */	XawVendorShellSetValues,
    /* set_values_hook	  */	NULL,
    /* set_values_almost  */	XtInheritSetValuesAlmost,
    /* get_values_hook	  */	NULL,
    /* accept_focus	  */	NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets	  */	NULL,
    /* tm_table		  */	NULL,
    /* query_geometry	  */	NULL,
    /* display_accelerator*/	NULL,
    /* extension	  */	NULL
  },{
    /* geometry_manager	  */	XawVendorShellGeometryManager,
    /* change_managed	  */	XawVendorShellChangeManaged,
    /* insert_child	  */	XtInheritInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	  */	(XtPointer) &vendorCompositeExt
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  }
};

#if !defined(__UNIXOS2__) && !defined(__APPLE__)
externaldef(vendorshellwidgetclass) WidgetClass vendorShellWidgetClass =
	(WidgetClass) (&vendorShellClassRec);
#endif

/***************************************************************************
 *
 * The following section is for the Vendor shell Extension class record
 *
 ***************************************************************************/

static XtResource ext_resources[] = {
  {XtNinputMethod, XtCInputMethod, XtRString, sizeof(String),
		XtOffsetOf(XawVendorShellExtRec, vendor_ext.im.input_method),
		XtRString, (XtPointer)NULL},
  {XtNpreeditType, XtCPreeditType, XtRString, sizeof(String),
		XtOffsetOf(XawVendorShellExtRec, vendor_ext.im.preedit_type),
		XtRString, (XtPointer)"OverTheSpot,OffTheSpot,Root"},
  {XtNopenIm, XtCOpenIm, XtRBoolean, sizeof(Boolean),
		XtOffsetOf(XawVendorShellExtRec, vendor_ext.im.open_im),
		XtRImmediate, (XtPointer)TRUE},
  {XtNsharedIc, XtCSharedIc, XtRBoolean, sizeof(Boolean),
		XtOffsetOf(XawVendorShellExtRec, vendor_ext.ic.shared_ic),
		XtRImmediate, (XtPointer)FALSE}
};

externaldef(vendorshellextclassrec) XawVendorShellExtClassRec
       xawvendorShellExtClassRec = {
  {
    /* superclass	  */	(WidgetClass)&objectClassRec,
    /* class_name	  */	"VendorShellExt",
    /* size		  */	sizeof(XawVendorShellExtRec),
    /* class_initialize	  */	XawVendorShellExtClassInitialize,
    /* class_part_initialize*/	NULL,
    /* Class init'ed ?	  */	FALSE,
    /* initialize	  */	XawVendorShellExtInitialize,
    /* initialize_hook	  */	NULL,
    /* pad		  */	NULL,
    /* pad		  */	NULL,
    /* pad		  */	0,
    /* resources	  */	ext_resources,
    /* resource_count	  */	XtNumber(ext_resources),
    /* xrm_class	  */	NULLQUARK,
    /* pad		  */	FALSE,
    /* pad		  */	FALSE,
    /* pad		  */	FALSE,
    /* pad		  */	FALSE,
    /* destroy		  */	XawVendorShellExtDestroy,
    /* pad		  */	NULL,
    /* pad		  */	NULL,
    /* set_values	  */	XawVendorShellExtSetValues,
    /* set_values_hook	  */	NULL,
    /* pad		  */	NULL,
    /* get_values_hook	  */	NULL,
    /* pad		  */	NULL,
    /* version		  */	XtVersion,
    /* callback_offsets	  */	NULL,
    /* pad		  */	NULL,
    /* pad		  */	NULL,
    /* pad		  */	NULL,
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  }
};

externaldef(xawvendorshellwidgetclass) WidgetClass
     xawvendorShellExtWidgetClass = (WidgetClass) (&xawvendorShellExtClassRec);


/*ARGSUSED*/
static Boolean
XawCvtCompoundTextToString(Display *dpy, XrmValuePtr args _X_UNUSED, Cardinal *num_args _X_UNUSED,
			   XrmValue *fromVal, XrmValue *toVal,
			   XtPointer *cvt_data _X_UNUSED)
{
    XTextProperty prop;
    char **list;
    int count;
    static char *mbs = NULL;
    int len;

    prop.value = (unsigned char *)fromVal->addr;
    prop.encoding = XA_COMPOUND_TEXT(dpy);
    prop.format = 8;
    prop.nitems = fromVal->size;

    if(XmbTextPropertyToTextList(dpy, &prop, &list, &count) < Success) {
	XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
	"converter", "XmbTextPropertyToTextList", "XawError",
	"conversion from CT to MB failed.", NULL, NULL);
	return False;
    }
    len = (int)strlen(*list);
    toVal->size = (unsigned)len;
    mbs = XtRealloc(mbs, (Cardinal)(len + 1)); /* keep buffer because no one call free :( */
    strcpy(mbs, *list);
    XFreeStringList(list);
    toVal->addr = (XtPointer)mbs;
    return True;
}

static void
XawVendorShellClassInitialize(void)
{
    static XtConvertArgRec screenConvertArg[] = {
        {XtWidgetBaseOffset, (XtPointer) XtOffsetOf(WidgetRec, core.screen),
	     sizeof(Screen *)}
    };

    XtAddConverter(XtRString, XtRCursor, XmuCvtStringToCursor,
		   screenConvertArg, XtNumber(screenConvertArg));

    XtAddConverter(XtRString, XtRBitmap, XmuCvtStringToBitmap,
		   screenConvertArg, XtNumber(screenConvertArg));

    XtSetTypeConverter("CompoundText", XtRString, XawCvtCompoundTextToString,
			NULL, 0, XtCacheNone, NULL);
}

static void
XawVendorShellClassPartInit(WidgetClass cclass)
{
    CompositeClassExtension ext;
    VendorShellWidgetClass vsclass = (VendorShellWidgetClass)cclass;

    if ((ext = (CompositeClassExtension)
	    XtGetClassExtension (cclass,
				 XtOffsetOf(CompositeClassRec,
					    composite_class.extension),
				 NULLQUARK, 1L, (Cardinal) 0)) == NULL) {
	ext = (CompositeClassExtension) XtNew (CompositeClassExtensionRec);
	if (ext != NULL) {
	    ext->next_extension = vsclass->composite_class.extension;
	    ext->record_type = NULLQUARK;
	    ext->version = XtCompositeExtensionVersion;
	    ext->record_size = sizeof (CompositeClassExtensionRec);
	    ext->accepts_objects = TRUE;
	    ext->allows_change_managed_set = FALSE;
	    vsclass->composite_class.extension = (XtPointer) ext;
	}
    }
}

#if defined(__osf__) || defined(__UNIXOS2__) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__APPLE__)
/* stupid OSF/1 shared libraries have the wrong semantics */
/* symbols do not get resolved external to the shared library */
void _XawFixupVendorShell()
{
    transientShellWidgetClass->core_class.superclass =
        (WidgetClass) &vendorShellClassRec;
    topLevelShellWidgetClass->core_class.superclass =
        (WidgetClass) &vendorShellClassRec;
}
#endif

/* ARGSUSED */
static void
XawVendorShellInitialize(Widget req _X_UNUSED, Widget cnew,
			 ArgList args, Cardinal *num_args)
{
    XtAddEventHandler(cnew, (EventMask) 0, TRUE, _XEditResCheckMessages, NULL);
    XtAddEventHandler(cnew, (EventMask) 0, TRUE, XmuRegisterExternalAgent, NULL);
    XtCreateWidget("shellext", xawvendorShellExtWidgetClass,
		   cnew, args, *num_args);
}

/* ARGSUSED */
static Boolean
XawVendorShellSetValues(Widget old _X_UNUSED, Widget ref _X_UNUSED, Widget cnew _X_UNUSED,
			ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
	return FALSE;
}

static void
XawVendorShellRealize(Widget wid, Mask *vmask, XSetWindowAttributes *attr)
{
	WidgetClass super = wmShellWidgetClass;

	/* Make my superclass do all the dirty work */

	(*super->core_class.realize) (wid, vmask, attr);
	_XawImRealize(wid);
}


static void
XawVendorShellExtClassInitialize(void)
{
}

/* ARGSUSED */
static void
XawVendorShellExtInitialize(Widget req _X_UNUSED, Widget cnew,
			    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    _XawImInitialize(cnew->core.parent, cnew);
}

/* ARGSUSED */
static void
XawVendorShellExtDestroy(Widget w)
{
    _XawImDestroy( w->core.parent, w );
}

/* ARGSUSED */
static Boolean
XawVendorShellExtSetValues(Widget old _X_UNUSED, Widget ref _X_UNUSED, Widget cnew _X_UNUSED,
			   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
	return FALSE;
}

void
XawVendorShellExtResize(Widget w)
{
	ShellWidget sw = (ShellWidget) w;
	Widget childwid;
	Cardinal i;
	int core_height;

	_XawImResizeVendorShell( w );
	core_height = _XawImGetShellHeight( w );
	for( i = 0; i < sw->composite.num_children; i++ ) {
	    if( XtIsManaged( sw->composite.children[ i ] ) ) {
		childwid = sw->composite.children[ i ];
		XtResizeWidget( childwid,
			       (Dimension)sw->core.width,
			       (Dimension)core_height,
			       childwid->core.border_width );
	    }
	}
}

/*ARGSUSED*/
void
XawVendorStructureNotifyHandler(Widget w, XtPointer closure _X_UNUSED, XEvent *event _X_UNUSED,
				Boolean *continue_to_dispatch _X_UNUSED)
{
  XawVendorShellExtResize(w);
}

/*ARGSUSED*/
static XtGeometryResult
XawVendorShellGeometryManager(Widget wid, XtWidgetGeometry *request,
			      XtWidgetGeometry *reply _X_UNUSED)
{
	ShellWidget shell = (ShellWidget)(wid->core.parent);
	XtWidgetGeometry my_request;

	if(shell->shell.allow_shell_resize == FALSE && XtIsRealized(wid))
		return(XtGeometryNo);

	if (request->request_mode & (CWX | CWY))
	    return(XtGeometryNo);

	/* %%% worry about XtCWQueryOnly */
	my_request.request_mode = 0;
	if (request->request_mode & CWWidth) {
	    my_request.width = request->width;
	    my_request.request_mode |= CWWidth;
	}
	if (request->request_mode & CWHeight) {
	    my_request.height = (Dimension)(request->height
			      + _XawImGetImAreaHeight( wid ));
	    my_request.request_mode |= CWHeight;
	}
	if (request->request_mode & CWBorderWidth) {
	    my_request.border_width = request->border_width;
	    my_request.request_mode |= CWBorderWidth;
	}
	if (XtMakeGeometryRequest((Widget)shell, &my_request, NULL)
		== XtGeometryYes) {
	    /* assert: if (request->request_mode & CWWidth) then
	     * 		  shell->core.width == request->width
	     * assert: if (request->request_mode & CWHeight) then
	     * 		  shell->core.height == request->height
	     *
	     * so, whatever the WM sized us to (if the Shell requested
	     * only one of the two) is now the correct child size
	     */

	    wid->core.width = shell->core.width;
	    wid->core.height = shell->core.height;
	    if (request->request_mode & CWBorderWidth) {
		wid->core.x = wid->core.y = (Position)(-request->border_width);
	    }
	    _XawImCallVendorShellExtResize(wid);
	    return XtGeometryYes;
	} else return XtGeometryNo;
}

static void
XawVendorShellChangeManaged(Widget wid)
{
	ShellWidget w = (ShellWidget) wid;
	Widget* childP;
	int i;

	(*SuperClass->composite_class.change_managed)(wid);
	for (i = (int)w->composite.num_children, childP = w->composite.children;
	     i; i--, childP++) {
	    if (XtIsManaged(*childP)) {
		XtSetKeyboardFocus(wid, *childP);
		break;
	    }
	}
}
