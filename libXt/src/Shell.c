/***********************************************************
Copyright (c) 1993, Oracle and/or its affiliates. All rights reserved.

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

#ifndef DEFAULT_WM_TIMEOUT
#define DEFAULT_WM_TIMEOUT 5000
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "StringDefs.h"
#include "Shell.h"
#include "ShellP.h"
#include "ShellI.h"
#include "Vendor.h"
#include "VendorP.h"
#include <X11/Xatom.h>
#include <X11/Xlocale.h>
#include <X11/ICE/ICElib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef EDITRES
#include <X11/Xmu/Editres.h>
#endif

/***************************************************************************
 *
 * Note: per the Xt spec, the Shell geometry management assumes in
 * several places that there is only one managed child.  This is
 * *not* a bug.  Any subclass that assumes otherwise is broken.
 *
 ***************************************************************************/

#define BIGSIZE ((Dimension)32767)

/***************************************************************************
 *
 * Default values for resource lists
 *
 ***************************************************************************/

static void _XtShellDepth(Widget, int, XrmValue *);
static void _XtShellColormap(Widget, int, XrmValue *);
static void _XtShellAncestorSensitive(Widget, int, XrmValue *);
static void _XtTitleEncoding(Widget, int, XrmValue *);

/***************************************************************************
 *
 * Shell class record
 *
 ***************************************************************************/

#define Offset(x)	(XtOffsetOf(ShellRec, x))
static XtResource shellResources[]=
{
	{XtNx, XtCPosition, XtRPosition, sizeof(Position),
	    Offset(core.x), XtRImmediate, (XtPointer)BIGSIZE},
	{XtNy, XtCPosition, XtRPosition, sizeof(Position),
	    Offset(core.y), XtRImmediate, (XtPointer)BIGSIZE},
	{ XtNdepth, XtCDepth, XtRInt, sizeof(int),
	    Offset(core.depth), XtRCallProc, (XtPointer) _XtShellDepth},
	{ XtNcolormap, XtCColormap, XtRColormap, sizeof(Colormap),
	    Offset(core.colormap), XtRCallProc, (XtPointer) _XtShellColormap},
	{ XtNancestorSensitive, XtCSensitive, XtRBoolean, sizeof(Boolean),
	    Offset(core.ancestor_sensitive), XtRCallProc,
	    (XtPointer) _XtShellAncestorSensitive},
	{ XtNallowShellResize, XtCAllowShellResize, XtRBoolean,
	    sizeof(Boolean), Offset(shell.allow_shell_resize),
	    XtRImmediate, (XtPointer)False},
	{ XtNgeometry, XtCGeometry, XtRString, sizeof(String),
	    Offset(shell.geometry), XtRString, (XtPointer)NULL},
	{ XtNcreatePopupChildProc, XtCCreatePopupChildProc, XtRFunction,
	    sizeof(XtCreatePopupChildProc), Offset(shell.create_popup_child_proc),
	    XtRFunction, NULL},
	{ XtNsaveUnder, XtCSaveUnder, XtRBoolean, sizeof(Boolean),
	    Offset(shell.save_under), XtRImmediate, (XtPointer)False},
	{ XtNpopupCallback, XtCCallback, XtRCallback, sizeof(XtCallbackList),
	    Offset(shell.popup_callback), XtRCallback, (XtPointer) NULL},
	{ XtNpopdownCallback, XtCCallback, XtRCallback, sizeof(XtCallbackList),
	    Offset(shell.popdown_callback), XtRCallback, (XtPointer) NULL},
	{ XtNoverrideRedirect, XtCOverrideRedirect,
	    XtRBoolean, sizeof(Boolean), Offset(shell.override_redirect),
	    XtRImmediate, (XtPointer)False},
	{ XtNvisual, XtCVisual, XtRVisual, sizeof(Visual*),
	    Offset(shell.visual), XtRImmediate, (XtPointer)CopyFromParent}
};

static void ClassPartInitialize(WidgetClass);
static void Initialize(Widget, Widget, ArgList, Cardinal *);
static void Realize(Widget, Mask *, XSetWindowAttributes *);
static void Resize(Widget);
static Boolean SetValues(Widget, Widget, Widget, ArgList , Cardinal *);
static void GetValuesHook(Widget, ArgList, Cardinal*);
static void ChangeManaged(Widget);
static XtGeometryResult GeometryManager(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static XtGeometryResult RootGeometryManager(Widget gw, XtWidgetGeometry *request, XtWidgetGeometry *reply);
static void Destroy(Widget);

static ShellClassExtensionRec shellClassExtRec = {
    NULL,
    NULLQUARK,
    XtShellExtensionVersion,
    sizeof(ShellClassExtensionRec),
    RootGeometryManager
};

externaldef(shellclassrec) ShellClassRec shellClassRec = {
  {   /* Core */
    /* superclass	  */	(WidgetClass) &compositeClassRec,
    /* class_name	  */	"Shell",
    /* size		  */	sizeof(ShellRec),
    /* Class Initializer  */	NULL,
    /* class_part_initialize*/	ClassPartInitialize,
    /* Class init'ed ?	  */	FALSE,
    /* initialize	  */	Initialize,
    /* initialize_notify  */	NULL,
    /* realize		  */	Realize,
    /* actions		  */	NULL,
    /* num_actions	  */	0,
    /* resources	  */	shellResources,
    /* resource_count	  */	XtNumber(shellResources),
    /* xrm_class	  */	NULLQUARK,
    /* compress_motion	  */	FALSE,
    /* compress_exposure  */	TRUE,
    /* compress_enterleave*/	FALSE,
    /* visible_interest	  */	FALSE,
    /* destroy		  */	Destroy,
    /* resize		  */	Resize,
    /* expose		  */	NULL,
    /* set_values	  */	SetValues,
    /* set_values_hook	  */	NULL,
    /* set_values_almost  */	XtInheritSetValuesAlmost,
    /* get_values_hook	  */	GetValuesHook,
    /* accept_focus	  */	NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets	  */	NULL,
    /* tm_table		  */	NULL,
    /* query_geometry	  */	NULL,
    /* display_accelerator*/	NULL,
    /* extension	  */	NULL
  },{ /* Composite */
    /* geometry_manager	  */	GeometryManager,
    /* change_managed	  */	ChangeManaged,
    /* insert_child	  */	XtInheritInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	  */	NULL
  },{ /* Shell */
    /* extension	  */	(XtPointer)&shellClassExtRec
  }
};

externaldef(shellwidgetclass) WidgetClass shellWidgetClass = (WidgetClass) (&shellClassRec);

/***************************************************************************
 *
 * OverrideShell class record
 *
 ***************************************************************************/

static XtResource overrideResources[]=
{
	{ XtNoverrideRedirect, XtCOverrideRedirect,
	    XtRBoolean, sizeof(Boolean), Offset(shell.override_redirect),
	    XtRImmediate, (XtPointer)True},
	{ XtNsaveUnder, XtCSaveUnder, XtRBoolean, sizeof(Boolean),
	    Offset(shell.save_under), XtRImmediate, (XtPointer)True},
};

externaldef(overrideshellclassrec) OverrideShellClassRec overrideShellClassRec = {
  {
    /* superclass         */    (WidgetClass) &shellClassRec,
    /* class_name         */    "OverrideShell",
    /* size               */    sizeof(OverrideShellRec),
    /* Class Initializer  */	NULL,
    /* class_part_initialize*/	NULL,
    /* Class init'ed ?    */	FALSE,
    /* initialize         */    NULL,
    /* initialize_notify    */	NULL,
    /* realize            */    XtInheritRealize,
    /* actions            */    NULL,
    /* num_actions        */    0,
    /* resources          */    overrideResources,
    /* resource_count     */	XtNumber(overrideResources),
    /* xrm_class          */    NULLQUARK,
    /* compress_motion    */    FALSE,
    /* compress_exposure  */    TRUE,
    /* compress_enterleave*/ 	FALSE,
    /* visible_interest   */    FALSE,
    /* destroy            */    NULL,
    /* resize             */    XtInheritResize,
    /* expose             */    NULL,
    /* set_values         */    NULL,
    /* set_values_hook      */	NULL,
    /* set_values_almost    */	XtInheritSetValuesAlmost,
    /* get_values_hook      */	NULL,
    /* accept_focus       */    NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets   */    NULL,
    /* tm_table		    */  NULL,
    /* query_geometry	    */  NULL,
    /* display_accelerator  */  NULL,
    /* extension	    */  NULL
  },{
    /* geometry_manager   */    XtInheritGeometryManager,
    /* change_managed     */    XtInheritChangeManaged,
    /* insert_child	  */	XtInheritInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  }
};

externaldef(overrideshellwidgetclass) WidgetClass overrideShellWidgetClass =
	(WidgetClass) (&overrideShellClassRec);

/***************************************************************************
 *
 * WMShell class record
 *
 ***************************************************************************/

#undef Offset
#define Offset(x)	(XtOffsetOf(WMShellRec, x))

static int default_unspecified_shell_int = XtUnspecifiedShellInt;
/*
 * Warning, casting XtUnspecifiedShellInt (which is -1) to an (XtPointer)
 * can result is loss of bits on some machines (i.e. crays)
 */

static XtResource wmResources[]=
{
	{ XtNtitle, XtCTitle, XtRString, sizeof(String),
	    Offset(wm.title), XtRString, NULL},
	{ XtNtitleEncoding, XtCTitleEncoding, XtRAtom, sizeof(Atom),
	    Offset(wm.title_encoding),
	    XtRCallProc, (XtPointer) _XtTitleEncoding},
	{ XtNwmTimeout, XtCWmTimeout, XtRInt, sizeof(int),
	    Offset(wm.wm_timeout), XtRImmediate,(XtPointer)DEFAULT_WM_TIMEOUT},
	{ XtNwaitForWm, XtCWaitForWm, XtRBoolean, sizeof(Boolean),
	    Offset(wm.wait_for_wm), XtRImmediate, (XtPointer)True},
	{ XtNtransient, XtCTransient, XtRBoolean, sizeof(Boolean),
	    Offset(wm.transient), XtRImmediate, (XtPointer)False},
/* size_hints minus things stored in core */
	{ XtNbaseWidth, XtCBaseWidth, XtRInt, sizeof(int),
	    Offset(wm.base_width),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNbaseHeight, XtCBaseHeight, XtRInt, sizeof(int),
	    Offset(wm.base_height),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNwinGravity, XtCWinGravity, XtRGravity, sizeof(int),
	    Offset(wm.win_gravity),
	    XtRGravity, (XtPointer) &default_unspecified_shell_int},
	{ XtNminWidth, XtCMinWidth, XtRInt, sizeof(int),
	    Offset(wm.size_hints.min_width),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNminHeight, XtCMinHeight, XtRInt, sizeof(int),
	    Offset(wm.size_hints.min_height),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNmaxWidth, XtCMaxWidth, XtRInt, sizeof(int),
	    Offset(wm.size_hints.max_width),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNmaxHeight, XtCMaxHeight, XtRInt, sizeof(int),
	    Offset(wm.size_hints.max_height),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNwidthInc, XtCWidthInc, XtRInt, sizeof(int),
	    Offset(wm.size_hints.width_inc),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNheightInc, XtCHeightInc, XtRInt, sizeof(int),
	    Offset(wm.size_hints.height_inc),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNminAspectX, XtCMinAspectX, XtRInt, sizeof(int),
	    Offset(wm.size_hints.min_aspect.x),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNminAspectY, XtCMinAspectY, XtRInt, sizeof(int),
	    Offset(wm.size_hints.min_aspect.y),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNmaxAspectX, XtCMaxAspectX, XtRInt, sizeof(int),
	    Offset(wm.size_hints.max_aspect.x),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNmaxAspectY, XtCMaxAspectY, XtRInt, sizeof(int),
	    Offset(wm.size_hints.max_aspect.y),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
/* wm_hints */
	{ XtNinput, XtCInput, XtRBool, sizeof(Bool),
	    Offset(wm.wm_hints.input), XtRImmediate, (XtPointer)False},
	{ XtNinitialState, XtCInitialState, XtRInitialState, sizeof(int),
	    Offset(wm.wm_hints.initial_state),
	    XtRImmediate, (XtPointer)NormalState},
	{ XtNiconPixmap, XtCIconPixmap, XtRBitmap, sizeof(Pixmap),
	    Offset(wm.wm_hints.icon_pixmap), XtRPixmap, NULL},
	{ XtNiconWindow, XtCIconWindow, XtRWindow, sizeof(Window),
	    Offset(wm.wm_hints.icon_window), XtRWindow,   (XtPointer) NULL},
	{ XtNiconX, XtCIconX, XtRInt, sizeof(int),
	    Offset(wm.wm_hints.icon_x),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNiconY, XtCIconY, XtRInt, sizeof(int),
	    Offset(wm.wm_hints.icon_y),
	    XtRInt, (XtPointer) &default_unspecified_shell_int},
	{ XtNiconMask, XtCIconMask, XtRBitmap, sizeof(Pixmap),
	    Offset(wm.wm_hints.icon_mask), XtRPixmap, NULL},
	{ XtNwindowGroup, XtCWindowGroup, XtRWindow, sizeof(Window),
	    Offset(wm.wm_hints.window_group),
	    XtRImmediate, (XtPointer)XtUnspecifiedWindow},
	{ XtNclientLeader, XtCClientLeader, XtRWidget, sizeof(Widget),
	    Offset(wm.client_leader), XtRWidget, NULL},
	{ XtNwindowRole, XtCWindowRole, XtRString, sizeof(String),
	    Offset(wm.window_role), XtRString, (XtPointer) NULL},
	{ XtNurgency, XtCUrgency, XtRBoolean, sizeof(Boolean),
	    Offset(wm.urgency), XtRImmediate, (XtPointer) False}
};

static void WMInitialize(Widget, Widget, ArgList, Cardinal *);
static Boolean WMSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static void WMDestroy(Widget);

externaldef(wmshellclassrec) WMShellClassRec wmShellClassRec = {
  {
    /* superclass         */    (WidgetClass) &shellClassRec,
    /* class_name         */    "WMShell",
    /* size               */    sizeof(WMShellRec),
    /* Class Initializer  */	NULL,
    /* class_part_initialize*/	NULL,
    /* Class init'ed ?    */	FALSE,
    /* initialize         */    WMInitialize,
    /* initialize_notify    */	NULL,
    /* realize            */    XtInheritRealize,
    /* actions            */    NULL,
    /* num_actions        */    0,
    /* resources          */    wmResources,
    /* resource_count     */	XtNumber(wmResources),
    /* xrm_class          */    NULLQUARK,
    /* compress_motion    */    FALSE,
    /* compress_exposure  */    TRUE,
    /* compress_enterleave*/	FALSE,
    /* visible_interest   */    FALSE,
    /* destroy            */    WMDestroy,
    /* resize             */    XtInheritResize,
    /* expose             */    NULL,
    /* set_values         */    WMSetValues,
    /* set_values_hook      */	NULL,
    /* set_values_almost    */	XtInheritSetValuesAlmost,
    /* get_values_hook      */	NULL,
    /* accept_focus       */    NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets   */    NULL,
    /* tm_table		    */  NULL,
    /* query_geometry	    */  NULL,
    /* display_accelerator  */  NULL,
    /* extension	    */  NULL
  },{
    /* geometry_manager   */    XtInheritGeometryManager,
    /* change_managed     */    XtInheritChangeManaged,
    /* insert_child	  */	XtInheritInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  }
};

externaldef(wmshellwidgetclass) WidgetClass wmShellWidgetClass = (WidgetClass) (&wmShellClassRec);

/***************************************************************************
 *
 * TransientShell class record
 *
 ***************************************************************************/

#undef Offset
#define Offset(x)	(XtOffsetOf(TransientShellRec, x))

static XtResource transientResources[]=
{
	{ XtNtransient, XtCTransient, XtRBoolean, sizeof(Boolean),
	    Offset(wm.transient), XtRImmediate, (XtPointer)True},
	{ XtNtransientFor, XtCTransientFor, XtRWidget, sizeof(Widget),
	    Offset(transient.transient_for), XtRWidget, NULL},
	{ XtNsaveUnder, XtCSaveUnder, XtRBoolean, sizeof(Boolean),
	    Offset(shell.save_under), XtRImmediate, (XtPointer)True},
};

static void TransientRealize(Widget, Mask *, XSetWindowAttributes *);
static Boolean TransientSetValues(Widget, Widget, Widget, ArgList, Cardinal *);

externaldef(transientshellclassrec) TransientShellClassRec transientShellClassRec = {
  {
    /* superclass	  */	(WidgetClass) &vendorShellClassRec,
    /* class_name	  */	"TransientShell",
    /* size		  */	sizeof(TransientShellRec),
    /* Class Initializer  */	NULL,
    /* class_part_initialize*/	NULL,
    /* Class init'ed ?	  */	FALSE,
    /* initialize	  */	NULL,
    /* initialize_notify  */	NULL,
    /* realize		  */	TransientRealize,
    /* actions		  */	NULL,
    /* num_actions	  */	0,
    /* resources	  */	transientResources,
    /* resource_count	  */	XtNumber(transientResources),
    /* xrm_class	  */	NULLQUARK,
    /* compress_motion	  */	FALSE,
    /* compress_exposure  */	TRUE,
    /* compress_enterleave*/	FALSE,
    /* visible_interest	  */	FALSE,
    /* destroy		  */	NULL,
    /* resize		  */	XtInheritResize,
    /* expose		  */	NULL,
    /* set_values	  */	TransientSetValues,
    /* set_values_hook	  */	NULL,
    /* set_values_almost  */	XtInheritSetValuesAlmost,
    /* get_values_hook	  */	NULL,
    /* accept_focus	  */	NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets	  */	NULL,
    /* tm_table		  */	XtInheritTranslations,
    /* query_geometry	  */	NULL,
    /* display_accelerator*/	NULL,
    /* extension	  */	NULL
  },{
    /* geometry_manager	  */	XtInheritGeometryManager,
    /* change_managed	  */	XtInheritChangeManaged,
    /* insert_child	  */	XtInheritInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  }
};

externaldef(transientshellwidgetclass) WidgetClass transientShellWidgetClass =
	(WidgetClass) (&transientShellClassRec);

/***************************************************************************
 *
 * TopLevelShell class record
 *
 ***************************************************************************/

#undef Offset
#define Offset(x)	(XtOffsetOf(TopLevelShellRec, x))

static XtResource topLevelResources[]=
{
	{ XtNiconName, XtCIconName, XtRString, sizeof(String),
	    Offset(topLevel.icon_name), XtRString, (XtPointer) NULL},
	{ XtNiconNameEncoding, XtCIconNameEncoding, XtRAtom, sizeof(Atom),
	    Offset(topLevel.icon_name_encoding),
	    XtRCallProc, (XtPointer) _XtTitleEncoding},
	{ XtNiconic, XtCIconic, XtRBoolean, sizeof(Boolean),
	    Offset(topLevel.iconic), XtRImmediate, (XtPointer)False}
};

static void TopLevelInitialize(Widget, Widget, ArgList, Cardinal *);
static Boolean TopLevelSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static void TopLevelDestroy(Widget);

externaldef(toplevelshellclassrec) TopLevelShellClassRec topLevelShellClassRec = {
  {
    /* superclass         */    (WidgetClass) &vendorShellClassRec,
    /* class_name         */    "TopLevelShell",
    /* size               */    sizeof(TopLevelShellRec),
    /* Class Initializer  */	NULL,
    /* class_part_initialize*/	NULL,
    /* Class init'ed ?    */	FALSE,
    /* initialize         */    TopLevelInitialize,
    /* initialize_notify    */	NULL,
    /* realize            */    XtInheritRealize,
    /* actions            */    NULL,
    /* num_actions        */    0,
    /* resources          */    topLevelResources,
    /* resource_count     */	XtNumber(topLevelResources),
    /* xrm_class          */    NULLQUARK,
    /* compress_motion    */    FALSE,
    /* compress_exposure  */    TRUE,
    /* compress_enterleave*/ 	FALSE,
    /* visible_interest   */    FALSE,
    /* destroy            */    TopLevelDestroy,
    /* resize             */    XtInheritResize,
    /* expose             */    NULL,
    /* set_values         */    TopLevelSetValues,
    /* set_values_hook      */	NULL,
    /* set_values_almost    */	XtInheritSetValuesAlmost,
    /* get_values_hook      */	NULL,
    /* accept_focus       */    NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets   */    NULL,
    /* tm_table		    */  XtInheritTranslations,
    /* query_geometry	    */  NULL,
    /* display_accelerator  */  NULL,
    /* extension	    */  NULL
  },{
    /* geometry_manager   */    XtInheritGeometryManager,
    /* change_managed     */    XtInheritChangeManaged,
    /* insert_child	  */	XtInheritInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  },{
    /* extension	    */  NULL
  }
};

externaldef(toplevelshellwidgetclass) WidgetClass topLevelShellWidgetClass =
	(WidgetClass) (&topLevelShellClassRec);

/***************************************************************************
 *
 * ApplicationShell class record
 *
 ***************************************************************************/

#undef Offset
#define Offset(x)	(XtOffsetOf(ApplicationShellRec, x))

static XtResource applicationResources[]=
{
    {XtNargc, XtCArgc, XtRInt, sizeof(int),
	  Offset(application.argc), XtRImmediate, (XtPointer)0},
    {XtNargv, XtCArgv, XtRStringArray, sizeof(String*),
	  Offset(application.argv), XtRPointer, (XtPointer) NULL}
};
#undef Offset

static void ApplicationInitialize(Widget, Widget, ArgList, Cardinal *);
static void ApplicationDestroy(Widget);
static Boolean ApplicationSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static void ApplicationShellInsertChild(Widget);

static CompositeClassExtensionRec compositeClassExtension = {
    /* next_extension	*/	NULL,
    /* record_type	*/	NULLQUARK,
    /* version		*/	XtCompositeExtensionVersion,
    /* record_size	*/	sizeof(CompositeClassExtensionRec),
    /* accepts_objects	*/	TRUE,
    /* allows_change_managed_set */ FALSE
};


externaldef(applicationshellclassrec) ApplicationShellClassRec applicationShellClassRec = {
  {
    /* superclass         */    (WidgetClass) &topLevelShellClassRec,
    /* class_name         */    "ApplicationShell",
    /* size               */    sizeof(ApplicationShellRec),
    /* Class Initializer  */	NULL,
    /* class_part_initialize*/	NULL,
    /* Class init'ed ?    */	FALSE,
    /* initialize         */    ApplicationInitialize,
    /* initialize_notify  */	NULL,
    /* realize            */    XtInheritRealize,
    /* actions            */    NULL,
    /* num_actions        */    0,
    /* resources          */    applicationResources,
    /* resource_count     */	XtNumber(applicationResources),
    /* xrm_class          */    NULLQUARK,
    /* compress_motion    */    FALSE,
    /* compress_exposure  */    TRUE,
    /* compress_enterleave*/    FALSE,
    /* visible_interest   */    FALSE,
    /* destroy            */    ApplicationDestroy,
    /* resize             */    XtInheritResize,
    /* expose             */    NULL,
    /* set_values         */    ApplicationSetValues,
    /* set_values_hook    */	NULL,
    /* set_values_almost  */	XtInheritSetValuesAlmost,
    /* get_values_hook    */	NULL,
    /* accept_focus       */    NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets   */    NULL,
    /* tm_table		  */	XtInheritTranslations,
    /* query_geometry	  */	NULL,
    /* display_accelerator*/	NULL,
    /* extension	  */	NULL
  },{
    /* geometry_manager   */    XtInheritGeometryManager,
    /* change_managed     */    XtInheritChangeManaged,
    /* insert_child	  */	ApplicationShellInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	  */	(XtPointer)&compositeClassExtension
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  }
};

externaldef(applicationshellwidgetclass) WidgetClass applicationShellWidgetClass =
	(WidgetClass) (&applicationShellClassRec);

/***************************************************************************
 *
 * SessionShell class record
 *
 ***************************************************************************/

#undef Offset
#define Offset(x)	(XtOffsetOf(SessionShellRec, x))

static XtResource sessionResources[]=
{
#ifndef XT_NO_SM
 {XtNconnection, XtCConnection, XtRSmcConn, sizeof(SmcConn),
       Offset(session.connection), XtRSmcConn, (XtPointer) NULL},
#endif
 {XtNsessionID, XtCSessionID, XtRString, sizeof(String),
       Offset(session.session_id), XtRString, (XtPointer) NULL},
 {XtNrestartCommand, XtCRestartCommand, XtRCommandArgArray, sizeof(String*),
       Offset(session.restart_command), XtRPointer, (XtPointer) NULL},
 {XtNcloneCommand, XtCCloneCommand, XtRCommandArgArray, sizeof(String*),
       Offset(session.clone_command), XtRPointer, (XtPointer) NULL},
 {XtNdiscardCommand, XtCDiscardCommand, XtRCommandArgArray, sizeof(String*),
       Offset(session.discard_command), XtRPointer, (XtPointer) NULL},
 {XtNresignCommand, XtCResignCommand, XtRCommandArgArray, sizeof(String*),
       Offset(session.resign_command), XtRPointer, (XtPointer) NULL},
 {XtNshutdownCommand, XtCShutdownCommand, XtRCommandArgArray, sizeof(String*),
       Offset(session.shutdown_command), XtRPointer, (XtPointer) NULL},
 {XtNenvironment, XtCEnvironment, XtREnvironmentArray, sizeof(String*),
       Offset(session.environment), XtRPointer, (XtPointer) NULL},
 {XtNcurrentDirectory, XtCCurrentDirectory, XtRDirectoryString, sizeof(String),
       Offset(session.current_dir), XtRString, (XtPointer) NULL},
 {XtNprogramPath, XtCProgramPath, XtRString, sizeof(String),
      Offset(session.program_path), XtRString, (XtPointer) NULL},
 {XtNrestartStyle, XtCRestartStyle, XtRRestartStyle, sizeof(unsigned char),
      Offset(session.restart_style), XtRImmediate,
      (XtPointer) SmRestartIfRunning},
 {XtNjoinSession, XtCJoinSession, XtRBoolean, sizeof(Boolean),
       Offset(session.join_session), XtRImmediate, (XtPointer) True},
 {XtNsaveCallback, XtCCallback, XtRCallback, sizeof(XtPointer),
       Offset(session.save_callbacks), XtRCallback, (XtPointer) NULL},
 {XtNinteractCallback, XtCCallback, XtRCallback, sizeof(XtPointer),
       Offset(session.interact_callbacks), XtRCallback, (XtPointer)NULL},
 {XtNcancelCallback, XtCCallback, XtRCallback, sizeof(XtPointer),
       Offset(session.cancel_callbacks), XtRCallback, (XtPointer) NULL},
 {XtNsaveCompleteCallback, XtCCallback, XtRCallback, sizeof(XtPointer),
       Offset(session.save_complete_callbacks), XtRCallback, (XtPointer) NULL},
 {XtNdieCallback, XtCCallback, XtRCallback, sizeof(XtPointer),
       Offset(session.die_callbacks), XtRCallback, (XtPointer) NULL},
 {XtNerrorCallback, XtCCallback, XtRCallback, sizeof(XtPointer),
       Offset(session.error_callbacks), XtRCallback, (XtPointer) NULL}
};
#undef Offset

static void SessionInitialize(Widget, Widget, ArgList, Cardinal *);
static void SessionDestroy(Widget);
static Boolean SessionSetValues(Widget, Widget, Widget, ArgList, Cardinal *);

static CompositeClassExtensionRec sessionCompositeClassExtension = {
    /* next_extension	*/	NULL,
    /* record_type	*/	NULLQUARK,
    /* version		*/	XtCompositeExtensionVersion,
    /* record_size	*/	sizeof(CompositeClassExtensionRec),
    /* accepts_objects	*/	TRUE,
    /* allows_change_managed_set */ FALSE
};


externaldef(sessionshellclassrec) SessionShellClassRec sessionShellClassRec = {
  {
    /* superclass         */    (WidgetClass) &applicationShellClassRec,
    /* class_name         */    "SessionShell",
    /* size               */    sizeof(SessionShellRec),
    /* Class Initializer  */	NULL,
    /* class_part_initialize*/	NULL,
    /* Class init'ed ?    */	FALSE,
    /* initialize         */    SessionInitialize,
    /* initialize_notify  */	NULL,
    /* realize            */    XtInheritRealize,
    /* actions            */    NULL,
    /* num_actions        */    0,
    /* resources          */    sessionResources,
    /* resource_count     */	XtNumber(sessionResources),
    /* xrm_class          */    NULLQUARK,
    /* compress_motion    */    FALSE,
    /* compress_exposure  */    TRUE,
    /* compress_enterleave*/    FALSE,
    /* visible_interest   */    FALSE,
    /* destroy            */    SessionDestroy,
    /* resize             */    XtInheritResize,
    /* expose             */    NULL,
    /* set_values         */    SessionSetValues,
    /* set_values_hook    */	NULL,
    /* set_values_almost  */	XtInheritSetValuesAlmost,
    /* get_values_hook    */	NULL,
    /* accept_focus       */    NULL,
    /* intrinsics version */	XtVersion,
    /* callback offsets   */    NULL,
    /* tm_table		  */	XtInheritTranslations,
    /* query_geometry	  */	NULL,
    /* display_accelerator*/	NULL,
    /* extension	  */	NULL
  },{
    /* geometry_manager   */    XtInheritGeometryManager,
    /* change_managed     */    XtInheritChangeManaged,
    /* insert_child	  */	XtInheritInsertChild,
    /* delete_child	  */	XtInheritDeleteChild,
    /* extension	  */	(XtPointer)&sessionCompositeClassExtension
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension	  */	NULL
  },{
    /* extension          */    NULL
  }
};

externaldef(sessionshellwidgetclass) WidgetClass sessionShellWidgetClass =
	(WidgetClass) (&sessionShellClassRec);

/****************************************************************************
 * Whew!
 ****************************************************************************/

static void ComputeWMSizeHints(
    WMShellWidget w,
    XSizeHints *hints)
{
    register long flags;
    hints->flags = flags = w->wm.size_hints.flags;
#define copy(field) hints->field = w->wm.size_hints.field
    if (flags & (USPosition | PPosition)) {
	copy(x);
	copy(y);
    }
    if (flags & (USSize | PSize)) {
	copy(width);
	copy(height);
    }
    if (flags & PMinSize) {
	copy(min_width);
	copy(min_height);
    }
    if (flags & PMaxSize) {
	copy(max_width);
	copy(max_height);
    }
    if (flags & PResizeInc) {
	copy(width_inc);
	copy(height_inc);
    }
    if (flags & PAspect) {
	copy(min_aspect.x);
	copy(min_aspect.y);
	copy(max_aspect.x);
	copy(max_aspect.y);
    }
#undef copy
#define copy(field) hints->field = w->wm.field
    if (flags & PBaseSize) {
	copy(base_width);
	copy(base_height);
    }
    if (flags & PWinGravity)
	copy(win_gravity);
#undef copy
}

static void _SetWMSizeHints(
    WMShellWidget w)
{
    XSizeHints *size_hints = XAllocSizeHints();

    if (size_hints == NULL) _XtAllocError("XAllocSizeHints");
    ComputeWMSizeHints(w, size_hints);
    XSetWMNormalHints(XtDisplay((Widget)w), XtWindow((Widget)w), size_hints);
    XFree((char*)size_hints);
}

static ShellClassExtension _FindClassExtension(
    WidgetClass widget_class)
{
    ShellClassExtension ext;
    for (ext = (ShellClassExtension)((ShellWidgetClass)widget_class)
	       ->shell_class.extension;
	 ext != NULL && ext->record_type != NULLQUARK;
	 ext = (ShellClassExtension)ext->next_extension);

    if (ext != NULL) {
	if (  ext->version == XtShellExtensionVersion
	      && ext->record_size == sizeof(ShellClassExtensionRec)) {
	    /* continue */
	} else {
	    String params[1];
	    Cardinal num_params = 1;
	    params[0] = widget_class->core_class.class_name;
	    XtErrorMsg( "invalidExtension", "shellClassPartInitialize",
		        XtCXtToolkitError,
		 "widget class %s has invalid ShellClassExtension record",
		 params, &num_params);
	}
    }
    return ext;
}

static void ClassPartInitialize(WidgetClass widget_class)
{
    ShellClassExtension ext = _FindClassExtension(widget_class);
    if (ext != NULL) {
	if (ext->root_geometry_manager == XtInheritRootGeometryManager) {
	    ext->root_geometry_manager =
		_FindClassExtension(widget_class->core_class.superclass)
		    ->root_geometry_manager;
	}
    } else {
	/* if not found, spec requires XtInheritRootGeometryManager */
	XtPointer *extP
	    = &((ShellWidgetClass)widget_class)->shell_class.extension;
	ext = XtNew(ShellClassExtensionRec);
	(void) memmove((char*)ext,
		       (char*)_FindClassExtension(widget_class->core_class.superclass),
		       sizeof(ShellClassExtensionRec));
	ext->next_extension = *extP;
	*extP = (XtPointer)ext;
    }
}


static void EventHandler(Widget wid, XtPointer closure, XEvent *event, Boolean *continue_to_dispatch);
static void _popup_set_prop(ShellWidget);


/*ARGSUSED*/
static void XtCopyDefaultDepth(
    Widget      widget,
    int		offset,
    XrmValue    *value)
{
    value->addr = (XPointer)(&DefaultDepthOfScreen(XtScreenOfObject(widget)));
}

static
void _XtShellDepth(
    Widget widget,
    int closure,
    XrmValue *value)
{
   if (widget->core.parent == NULL) XtCopyDefaultDepth(widget,closure,value);
   else _XtCopyFromParent (widget,closure,value);
}

/*ARGSUSED*/
static void XtCopyDefaultColormap(
    Widget      widget,
    int		offset,
    XrmValue    *value)
{
    value->addr = (XPointer)(&DefaultColormapOfScreen(XtScreenOfObject(widget)));
}

static
void _XtShellColormap(
    Widget widget,
    int closure,
    XrmValue *value)
{
   if (widget->core.parent == NULL)
	   XtCopyDefaultColormap(widget,closure,value);
   else _XtCopyFromParent (widget,closure,value);
}

static
void _XtShellAncestorSensitive(
    Widget widget,
    int closure,
    XrmValue *value)
{
   static Boolean true = True;
   if (widget->core.parent == NULL) value->addr = (XPointer)(&true);
   else _XtCopyFromParent (widget,closure,value);
}

/*ARGSUSED*/
static
void _XtTitleEncoding(
    Widget widget,
    int offset,
    XrmValue *value)
{
    static Atom atom;
    if (XtWidgetToApplicationContext(widget)->langProcRec.proc) atom = None;
    else atom = XA_STRING;
    value->addr = (XPointer) &atom;
}


/* ARGSUSED */
static void Initialize(
	Widget req,
	Widget new,
	ArgList args,		/* unused */
	Cardinal *num_args)	/* unused */
{
	ShellWidget w = (ShellWidget) new;

	w->shell.popped_up = FALSE;
	w->shell.client_specified =
	    _XtShellNotReparented | _XtShellPositionValid;

	if (w->core.x == BIGSIZE) {
	    w->core.x = 0;
	    if (w->core.y == BIGSIZE) w->core.y = 0;
	} else {
	    if (w->core.y == BIGSIZE) w->core.y = 0;
	    else w->shell.client_specified |= _XtShellPPositionOK;
	}

	XtAddEventHandler(new, (EventMask) StructureNotifyMask,
		TRUE, EventHandler, (XtPointer) NULL);

#ifdef EDITRES
	XtAddEventHandler(new, (EventMask) 0, TRUE,
			  _XEditResCheckMessages, NULL);
#endif
}

/* ARGSUSED */
static void WMInitialize(
	Widget req, Widget new,
	ArgList args,		/* unused */
	Cardinal *num_args)	/* unused */
{
	WMShellWidget w = (WMShellWidget) new;
	TopLevelShellWidget tls = (TopLevelShellWidget) new;	/* maybe */

	if(w->wm.title == NULL) {
	    if (XtIsTopLevelShell(new) &&
		    tls->topLevel.icon_name != NULL &&
		    strlen(tls->topLevel.icon_name) != 0) {
		w->wm.title = XtNewString(tls->topLevel.icon_name);
	    } else {
		w->wm.title = XtNewString(w->core.name);
	    }
	} else {
	    w->wm.title = XtNewString(w->wm.title);
	}
	w->wm.size_hints.flags = 0;
	w->wm.wm_hints.flags = 0;
	if (w->wm.window_role)
	    w->wm.window_role = XtNewString(w->wm.window_role);
}


/* ARGSUSED */
static void TopLevelInitialize(
	Widget req, Widget new,
	ArgList args,		/* unused */
	Cardinal *num_args)	/* unused */
{
	TopLevelShellWidget w = (TopLevelShellWidget) new;

	if (w->topLevel.icon_name == NULL) {
	    w->topLevel.icon_name = XtNewString(w->core.name);
	} else {
	    w->topLevel.icon_name = XtNewString(w->topLevel.icon_name);
	}

	if (w->topLevel.iconic)
	    w->wm.wm_hints.initial_state = IconicState;
}

static String *NewArgv(int, String *);
static String *NewStringArray(String *);
static void FreeStringArray(String *);

/* ARGSUSED */
static void ApplicationInitialize(
    Widget req, Widget new,
    ArgList args,		/* unused */
    Cardinal *num_args)		/* unused */
{
    ApplicationShellWidget w = (ApplicationShellWidget)new;

    if (w->application.argc > 0)
	w->application.argv = NewArgv(w->application.argc,
				      w->application.argv);
}

#define XtSaveInactive 0
#define XtSaveActive   1
#define XtInteractPending    2
#define XtInteractActive     3

#define XtCloneCommandMask	(1L<<0)
#define XtCurrentDirectoryMask	(1L<<1)
#define XtDiscardCommandMask	(1L<<2)
#define XtEnvironmentMask	(1L<<3)
#define XtProgramMask		(1L<<4)
#define XtResignCommandMask	(1L<<5)
#define XtRestartCommandMask	(1L<<6)
#define XtRestartStyleHintMask	(1L<<7)
#define XtShutdownCommandMask	(1L<<8)

static void JoinSession(SessionShellWidget);
static void SetSessionProperties(SessionShellWidget, Boolean, unsigned long, unsigned long);
static void StopManagingSession(SessionShellWidget, SmcConn);

typedef struct _XtSaveYourselfRec {
    XtSaveYourself next;
    int            save_type;
    int            interact_style;
    Boolean        shutdown;
    Boolean        fast;
    Boolean        cancel_shutdown;
    int		   phase;
    int            interact_dialog_type;
    Boolean	   request_cancel;
    Boolean	   request_next_phase;
    Boolean        save_success;
    int            save_tokens;
    int            interact_tokens;
} XtSaveYourselfRec;

/* ARGSUSED */
static void SessionInitialize(
    Widget req, Widget new,
    ArgList args,		/* unused */
    Cardinal *num_args)		/* unused */
{
#ifndef XT_NO_SM
    SessionShellWidget w = (SessionShellWidget)new;

    if (w->session.session_id) w->session.session_id =
	XtNewString(w->session.session_id);
    if (w->session.restart_command) w->session.restart_command =
	NewStringArray(w->session.restart_command);
    if (w->session.clone_command) w->session.clone_command =
	NewStringArray(w->session.clone_command);
    if (w->session.discard_command) w->session.discard_command =
	NewStringArray(w->session.discard_command);
    if (w->session.resign_command) w->session.resign_command =
	NewStringArray(w->session.resign_command);
    if (w->session.shutdown_command) w->session.shutdown_command =
	NewStringArray(w->session.shutdown_command);
    if (w->session.environment) w->session.environment =
	NewStringArray(w->session.environment);
    if (w->session.current_dir) w->session.current_dir =
	XtNewString(w->session.current_dir);
    if (w->session.program_path) w->session.program_path =
	XtNewString(w->session.program_path);

    w->session.checkpoint_state = XtSaveInactive;
    w->session.input_id = 0;
    w->session.save = NULL;

    if ((w->session.join_session) &&
	(w->application.argv || w->session.restart_command))
	JoinSession(w);

    if (w->session.connection)
	SetSessionProperties(w, True, 0L, 0L);
#endif /* !XT_NO_SM */
}

static void Resize(
    Widget w)
{
    register ShellWidget sw = (ShellWidget)w;
    Widget childwid;
    Cardinal i;
    for(i = 0; i < sw->composite.num_children; i++) {
        if (XtIsManaged(sw->composite.children[i])) {
             childwid = sw->composite.children[i];
             XtResizeWidget(childwid, sw->core.width, sw->core.height,
                           childwid->core.border_width);
	     break;		/* can only be one managed child */
        }
    }
}

static void GetGeometry(Widget, Widget);

static void Realize(
	Widget wid,
	Mask *vmask,
	XSetWindowAttributes *attr)
{
	ShellWidget w = (ShellWidget) wid;
        Mask mask = *vmask;

	if (! (w->shell.client_specified & _XtShellGeometryParsed)) {
	    /* we'll get here only if there was no child the first
	       time we were realized.  If the shell was Unrealized
	       and then re-Realized, we probably don't want to
	       re-evaluate the defaults anyway.
	     */
	    GetGeometry(wid, (Widget)NULL);
	}
	else if (w->core.background_pixmap == XtUnspecifiedPixmap) {
	    /* I attempt to inherit my child's background to avoid screen flash
	     * if there is latency between when I get resized and when my child
	     * is resized.  Background=None is not satisfactory, as I want the
	     * user to get immediate feedback on the new dimensions (most
	     * particularly in the case of a non-reparenting wm).  It is
	     * especially important to have the server clear any old cruft
	     * from the display when I am resized larger.
	     */
	    register Widget *childP = w->composite.children;
	    int i;
	    for (i = w->composite.num_children; i; i--, childP++) {
		if (XtIsWidget(*childP) && XtIsManaged(*childP)) {
		    if ((*childP)->core.background_pixmap
			    != XtUnspecifiedPixmap) {
			mask &= ~(CWBackPixel);
			mask |= CWBackPixmap;
			attr->background_pixmap =
			    w->core.background_pixmap =
				(*childP)->core.background_pixmap;
		    } else {
			attr->background_pixel =
			    w->core.background_pixel =
				(*childP)->core.background_pixel;
		    }
		    break;
		}
	    }
	}

	if(w->shell.save_under) {
		mask |= CWSaveUnder;
		attr->save_under = TRUE;
	}
	if(w->shell.override_redirect) {
		mask |= CWOverrideRedirect;
		attr->override_redirect = TRUE;
	}
	if (wid->core.width == 0 || wid->core.height == 0) {
	    Cardinal count = 1;
	    XtErrorMsg("invalidDimension", "shellRealize", XtCXtToolkitError,
		       "Shell widget %s has zero width and/or height",
		       &wid->core.name, &count);
	}
	wid->core.window = XCreateWindow(XtDisplay(wid),
		wid->core.screen->root, (int)wid->core.x, (int)wid->core.y,
		(unsigned int)wid->core.width, (unsigned int)wid->core.height,
		(unsigned int)wid->core.border_width, (int) wid->core.depth,
		(unsigned int) InputOutput, w->shell.visual,
		mask, attr);

	_popup_set_prop(w);
}


static void _SetTransientForHint(
     TransientShellWidget w,
     Boolean delete)
{
    Window window_group;

    if (w->wm.transient) {
	if (w->transient.transient_for != NULL
	    && XtIsRealized(w->transient.transient_for))
	    window_group = XtWindow(w->transient.transient_for);
	else if ((window_group = w->wm.wm_hints.window_group)
		 == XtUnspecifiedWindowGroup) {
	    if (delete)
		XDeleteProperty( XtDisplay((Widget)w),
				 XtWindow((Widget)w),
				 XA_WM_TRANSIENT_FOR
				);
	    return;
	}

	XSetTransientForHint( XtDisplay((Widget)w),
			      XtWindow((Widget)w),
			      window_group
			     );
    }
}


static void TransientRealize(
     Widget w,
     Mask *vmask,
     XSetWindowAttributes *attr)
{
    XtRealizeProc realize;

    LOCK_PROCESS;
    realize =
	transientShellWidgetClass->core_class.superclass->core_class.realize;
    UNLOCK_PROCESS;
    (*realize) (w, vmask, attr);

    _SetTransientForHint((TransientShellWidget)w, False);
}

static Widget GetClientLeader(
    Widget w)
{
    while ((! XtIsWMShell(w) || ! ((WMShellWidget)w)->wm.client_leader)
	   && w->core.parent)
	w = w->core.parent;

    /* ASSERT: w is a WMshell with client_leader set, or w has no parent */

    if (XtIsWMShell(w) && ((WMShellWidget)w)->wm.client_leader)
	w = ((WMShellWidget)w)->wm.client_leader;
    return w;
}

static void EvaluateWMHints(
    WMShellWidget w)
{
	XWMHints *hintp = &w->wm.wm_hints;

	hintp->flags = StateHint | InputHint;

	if (hintp->icon_x == XtUnspecifiedShellInt)
	    hintp->icon_x = -1;
	else
	    hintp->flags |= IconPositionHint;

	if (hintp->icon_y == XtUnspecifiedShellInt)
	    hintp->icon_y = -1;
	else
	    hintp->flags |= IconPositionHint;

	if (hintp->icon_pixmap != None) hintp->flags |= IconPixmapHint;
	if (hintp->icon_mask != None)   hintp->flags |= IconMaskHint;
	if (hintp->icon_window != None) hintp->flags |= IconWindowHint;

	if (hintp->window_group == XtUnspecifiedWindow) {
	    if(w->core.parent) {
		Widget p;
		for (p = w->core.parent; p->core.parent; p = p->core.parent);
		if (XtIsRealized(p)) {
		    hintp->window_group = XtWindow(p);
		    hintp->flags |=  WindowGroupHint;
		}
	    }
	} else if (hintp->window_group != XtUnspecifiedWindowGroup)
	    hintp->flags |=  WindowGroupHint;

	if (w->wm.urgency) hintp->flags |= XUrgencyHint;
}


static void EvaluateSizeHints(
    WMShellWidget w)
{
	struct _OldXSizeHints *sizep = &w->wm.size_hints;

	sizep->x = w->core.x;
	sizep->y = w->core.y;
	sizep->width = w->core.width;
	sizep->height = w->core.height;

	if (sizep->flags & USSize) {
	    if (sizep->flags & PSize) sizep->flags &= ~PSize;
	} else
	    sizep->flags |= PSize;

	if (sizep->flags & USPosition) {
	    if (sizep->flags & PPosition) sizep->flags &= ~PPosition;
	} else if (w->shell.client_specified & _XtShellPPositionOK)
	    sizep->flags |= PPosition;

	if (sizep->min_aspect.x != XtUnspecifiedShellInt
	    || sizep->min_aspect.y != XtUnspecifiedShellInt
	    || sizep->max_aspect.x != XtUnspecifiedShellInt
	    || sizep->max_aspect.y != XtUnspecifiedShellInt) {
	    sizep->flags |= PAspect;
	}
	if (sizep->flags & PBaseSize
	    || w->wm.base_width != XtUnspecifiedShellInt
	    || w->wm.base_height != XtUnspecifiedShellInt) {
	    sizep->flags |= PBaseSize;
	    if (w->wm.base_width == XtUnspecifiedShellInt)
		w->wm.base_width = 0;
	    if (w->wm.base_height == XtUnspecifiedShellInt)
		w->wm.base_height = 0;
	}
	if (sizep->flags & PResizeInc
	    || sizep->width_inc != XtUnspecifiedShellInt
	    || sizep->height_inc != XtUnspecifiedShellInt) {
	    if (sizep->width_inc < 1) sizep->width_inc = 1;
	    if (sizep->height_inc < 1) sizep->height_inc = 1;
	    sizep->flags |= PResizeInc;
	}
	if (sizep->flags & PMaxSize
	    || sizep->max_width != XtUnspecifiedShellInt
	    || sizep->max_height != XtUnspecifiedShellInt) {
	    sizep->flags |= PMaxSize;
	    if (sizep->max_width == XtUnspecifiedShellInt)
		sizep->max_width = BIGSIZE;
	    if (sizep->max_height == XtUnspecifiedShellInt)
		sizep->max_height = BIGSIZE;
	}
	if (sizep->flags & PMinSize
	    || sizep->min_width != XtUnspecifiedShellInt
	    || sizep->min_height != XtUnspecifiedShellInt) {
	    sizep->flags |= PMinSize;
	    if (sizep->min_width == XtUnspecifiedShellInt)
		sizep->min_width = 1;
	    if (sizep->min_height == XtUnspecifiedShellInt)
		sizep->min_height = 1;
	}
}

static void _popup_set_prop(
	ShellWidget w)
{
	Widget p;
	WMShellWidget wmshell = (WMShellWidget) w;
	TopLevelShellWidget tlshell = (TopLevelShellWidget) w;
	ApplicationShellWidget appshell = (ApplicationShellWidget) w;
	XTextProperty icon_name;
	XTextProperty window_name;
	char **argv;
	int argc;
	XSizeHints *size_hints;
	Window window_group;
	XClassHint classhint;
	Boolean copied_iname, copied_wname;

	if (!XtIsWMShell((Widget)w) || w->shell.override_redirect) return;

	if ((size_hints = XAllocSizeHints()) == NULL)
	    _XtAllocError("XAllocSizeHints");

	copied_iname = copied_wname = False;
        if (wmshell->wm.title_encoding == None &&
	    XmbTextListToTextProperty(XtDisplay((Widget)w),
				      (char**)&wmshell->wm.title,
				      1, XStdICCTextStyle,
				      &window_name) >= Success) {
	    copied_wname = True;
	} else {
	    window_name.value = (unsigned char*)wmshell->wm.title;
	    window_name.encoding = wmshell->wm.title_encoding ?
		wmshell->wm.title_encoding : XA_STRING;
	    window_name.format = 8;
	    window_name.nitems = strlen((char *)window_name.value);
	}

	if (XtIsTopLevelShell((Widget)w)) {
            if (tlshell->topLevel.icon_name_encoding == None &&
		XmbTextListToTextProperty(XtDisplay((Widget)w),
					  (char**)&tlshell->topLevel.icon_name,
					  1, XStdICCTextStyle,
					  &icon_name) >= Success) {
		copied_iname = True;
	    } else {
		icon_name.value = (unsigned char*)tlshell->topLevel.icon_name;
		icon_name.encoding = tlshell->topLevel.icon_name_encoding ?
		    tlshell->topLevel.icon_name_encoding : XA_STRING;
		icon_name.format = 8;
		icon_name.nitems = strlen((char *)icon_name.value);
	    }
	}

	EvaluateWMHints(wmshell);
	EvaluateSizeHints(wmshell);
	ComputeWMSizeHints(wmshell, size_hints);

	if (wmshell->wm.transient
	    && !XtIsTransientShell((Widget)w)
	    && (window_group = wmshell->wm.wm_hints.window_group)
	       != XtUnspecifiedWindowGroup) {

	    XSetTransientForHint(XtDisplay((Widget)w),
				 XtWindow((Widget)w),
				 window_group
				 );
	}

	classhint.res_name = w->core.name;
	/* For the class, look up to the top of the tree */
	for (p = (Widget)w; p->core.parent != NULL; p = p->core.parent);
	if (XtIsApplicationShell(p)) {
	    classhint.res_class =
		((ApplicationShellWidget)p)->application.class;
	} else {
	    LOCK_PROCESS;
	    classhint.res_class = XtClass(p)->core_class.class_name;
	    UNLOCK_PROCESS;
	}

	if (XtIsApplicationShell((Widget)w)
	    && (argc = appshell->application.argc) != -1)
	    argv = (char**)appshell->application.argv;
	else {
	    argv = NULL;
	    argc = 0;
	}

	XSetWMProperties(XtDisplay((Widget)w), XtWindow((Widget)w),
			 &window_name,
			 (XtIsTopLevelShell((Widget)w)) ? &icon_name : NULL,
			 argv, argc,
			 size_hints,
			 &wmshell->wm.wm_hints,
			 &classhint);
	XFree((char*)size_hints);
	if (copied_wname)
	    XFree((XPointer)window_name.value);
	if (copied_iname)
	    XFree((XPointer)icon_name.value);

	LOCK_PROCESS;
	if (XtWidgetToApplicationContext((Widget)w)->langProcRec.proc) {
	    char *locale = setlocale(LC_CTYPE, (char *)NULL);
	    if (locale)
		XChangeProperty(XtDisplay((Widget)w), XtWindow((Widget)w),
				XInternAtom(XtDisplay((Widget)w),
					    "WM_LOCALE_NAME", False),
				XA_STRING, 8, PropModeReplace,
				(unsigned char *)locale, strlen(locale));
	}
	UNLOCK_PROCESS;

	p = GetClientLeader((Widget)w);
	if (XtWindow(p))
	    XChangeProperty(XtDisplay((Widget)w), XtWindow((Widget)w),
			    XInternAtom(XtDisplay((Widget)w),
					"WM_CLIENT_LEADER", False),
			    XA_WINDOW, 32, PropModeReplace,
			    (unsigned char *)(&(p->core.window)), 1);
#ifndef XT_NO_SM
	if (p == (Widget) w) {
	    for ( ; p->core.parent != NULL; p = p->core.parent);
	    if (XtIsSubclass(p, sessionShellWidgetClass)) {
		String sm_client_id =
		    ((SessionShellWidget)p)->session.session_id;
		if (sm_client_id != NULL) {
		    XChangeProperty(XtDisplay((Widget)w), XtWindow((Widget)w),
				    XInternAtom(XtDisplay((Widget)w),
						"SM_CLIENT_ID", False),
				    XA_STRING, 8, PropModeReplace,
				    (unsigned char *) sm_client_id,
				    strlen(sm_client_id));
		}
	    }
	}
#endif /* !XT_NO_SM */

	if (wmshell->wm.window_role)
	    XChangeProperty(XtDisplay((Widget)w), XtWindow((Widget)w),
			    XInternAtom(XtDisplay((Widget)w),
					"WM_WINDOW_ROLE", False),
			    XA_STRING, 8, PropModeReplace,
			    (unsigned char *)wmshell->wm.window_role,
			    strlen(wmshell->wm.window_role));
}

/* ARGSUSED */
static void EventHandler(
	Widget wid,
	XtPointer closure,	/* unused */
	XEvent *event,
        Boolean *continue_to_dispatch) /* unused */
{
	register ShellWidget w = (ShellWidget) wid;
	WMShellWidget wmshell = (WMShellWidget) w;
	Boolean  sizechanged = FALSE;

	if(w->core.window != event->xany.window) {
		XtAppErrorMsg(XtWidgetToApplicationContext(wid),
			"invalidWindow","eventHandler",XtCXtToolkitError,
                        "Event with wrong window",
			(String *)NULL, (Cardinal *)NULL);
		return;
	}

	switch(event->type) {
	    case ConfigureNotify:
	        if (w->core.window != event->xconfigure.window)
		    return;  /* in case of SubstructureNotify */
#define NEQ(x)	( w->core.x != event->xconfigure.x )
		if( NEQ(width) || NEQ(height) || NEQ(border_width) ) {
			sizechanged = TRUE;
#undef NEQ
			w->core.width = event->xconfigure.width;
			w->core.height = event->xconfigure.height;
			w->core.border_width = event->xconfigure.border_width;
		}
		if (event->xany.send_event /* ICCCM compliant synthetic ev */
		    /* || w->shell.override_redirect */
		    || w->shell.client_specified & _XtShellNotReparented)
	        {
		    w->core.x = event->xconfigure.x;
		    w->core.y = event->xconfigure.y;
		    w->shell.client_specified |= _XtShellPositionValid;
		}
		else w->shell.client_specified &= ~_XtShellPositionValid;
		if (XtIsWMShell(wid) && !wmshell->wm.wait_for_wm) {
		    /* Consider trusting the wm again */
		    register struct _OldXSizeHints *hintp
			= &wmshell->wm.size_hints;
#define EQ(x) (hintp->x == w->core.x)
		    if (EQ(x) && EQ(y) && EQ(width) && EQ(height)) {
			wmshell->wm.wait_for_wm = TRUE;
		    }
#undef EQ
		}
		break;

	      case ReparentNotify:
		if (event->xreparent.window == XtWindow(w)) {
		   if (event->xreparent.parent !=
		       RootWindowOfScreen(XtScreen(w)))
		       w->shell.client_specified &=
			   ~(_XtShellNotReparented | _XtShellPositionValid);
		   else {
		       w->core.x = event->xreparent.x;
		       w->core.y = event->xreparent.y;
		       w->shell.client_specified |=
			   (_XtShellNotReparented | _XtShellPositionValid);
		   }
	        }
		return;

              case MapNotify:
                if (XtIsTopLevelShell(wid)) {
                    ((TopLevelShellWidget)wid)->topLevel.iconic = FALSE;
                }
                return;

	      case UnmapNotify:
		{
		    XtPerDisplayInput	pdi;
		    XtDevice		device;
		    Widget		p;

                    if (XtIsTopLevelShell(wid))
                        ((TopLevelShellWidget)wid)->topLevel.iconic = TRUE;

		    pdi = _XtGetPerDisplayInput(event->xunmap.display);

		    device = &pdi->pointer;
		    if (device->grabType == XtPassiveServerGrab) {
			p = device->grab.widget;
			while (p && !(XtIsShell(p)))
			    p = p->core.parent;
			if (p == wid)
			    device->grabType = XtNoServerGrab;
		    }

		    device = &pdi->keyboard;
		    if (IsEitherPassiveGrab(device->grabType)) {
			p = device->grab.widget;
			while (p && !(XtIsShell(p)))
			    p = p->core.parent;
			if (p == wid) {
			    device->grabType = XtNoServerGrab;
			    pdi->activatingKey = 0;
			}
		    }

		    return;
		}
	      default:
		 return;
	}
	{
	XtWidgetProc resize;

	LOCK_PROCESS;
	resize = XtClass(wid)->core_class.resize;
	UNLOCK_PROCESS;

	if (sizechanged && resize) {
	    CALLGEOTAT(_XtGeoTrace((Widget)w,
			   "Shell \"%s\" is being resized to %d %d.\n",
			   XtName(wid), wid->core.width, wid->core.height ));
	    (*resize)(wid);
	 }
	}
}

static void Destroy(
	Widget wid)
{
	if (XtIsRealized(wid))
	    XDestroyWindow( XtDisplay(wid), XtWindow(wid) );
}

static void WMDestroy(
	Widget wid)
{
	WMShellWidget w = (WMShellWidget) wid;

	XtFree((char *) w->wm.title);
	XtFree((char *) w->wm.window_role);
}

static void TopLevelDestroy(
	Widget wid)
{
	TopLevelShellWidget w = (TopLevelShellWidget) wid;

	XtFree((char *) w->topLevel.icon_name);
}

static void ApplicationDestroy(
    Widget wid)
{
    ApplicationShellWidget w = (ApplicationShellWidget) wid;
    if (w->application.argc > 0)
	FreeStringArray(w->application.argv);
}

static void SessionDestroy(
    Widget wid)
{
#ifndef XT_NO_SM
    SessionShellWidget w = (SessionShellWidget) wid;

    StopManagingSession(w, w->session.connection);
    XtFree(w->session.session_id);
    FreeStringArray(w->session.restart_command);
    FreeStringArray(w->session.clone_command);
    FreeStringArray(w->session.discard_command);
    FreeStringArray(w->session.resign_command);
    FreeStringArray(w->session.shutdown_command);
    FreeStringArray(w->session.environment);
    XtFree(w->session.current_dir);
    XtFree(w->session.program_path);
#endif /* !XT_NO_SM */
}

/*
 * If the Shell has a width and a height which are zero, and as such
 * suspect, and it has not yet been realized then it will grow to
 * match the child before parsing the geometry resource.
 *
 */
static void GetGeometry(
    Widget W, Widget child)
{
    register ShellWidget w = (ShellWidget)W;
    Boolean is_wmshell = XtIsWMShell(W);
    int x, y, width, height, win_gravity = -1, flag;
    XSizeHints hints;

    if (child != NULL) {
	/* we default to our child's size */
	if (is_wmshell && (w->core.width == 0 || w->core.height == 0))
	    ((WMShellWidget)W)->wm.size_hints.flags |= PSize;
	if (w->core.width == 0)	    w->core.width = child->core.width;
	if (w->core.height == 0)    w->core.height = child->core.height;
    }
    if(w->shell.geometry != NULL) {
	char def_geom[64];
	x = w->core.x;
	y = w->core.y;
	width = w->core.width;
	height = w->core.height;
	if (is_wmshell) {
	    WMShellPart* wm = &((WMShellWidget)w)->wm;
	    EvaluateSizeHints((WMShellWidget)w);
	    (void) memmove((char*)&hints, (char*)&wm->size_hints,
			   sizeof(struct _OldXSizeHints));
	    hints.win_gravity = wm->win_gravity;
	    if (wm->size_hints.flags & PBaseSize) {
		width -= wm->base_width;
		height -= wm->base_height;
		hints.base_width = wm->base_width;
		hints.base_height = wm->base_height;
	    }
	    else if (wm->size_hints.flags & PMinSize) {
		width -= wm->size_hints.min_width;
		height -= wm->size_hints.min_height;
	    }
	    if (wm->size_hints.flags & PResizeInc) {
		width /= wm->size_hints.width_inc;
		height /= wm->size_hints.height_inc;
	    }
	}
	else hints.flags = 0;

	snprintf( def_geom, sizeof(def_geom), "%dx%d+%d+%d",
		  width, height, x, y );
	flag = XWMGeometry( XtDisplay(W),
			    XScreenNumberOfScreen(XtScreen(W)),
			    w->shell.geometry, def_geom,
			    (unsigned int)w->core.border_width,
			    &hints, &x, &y, &width, &height,
			    &win_gravity
			   );
	if (flag) {
	    if (flag & XValue) w->core.x = (Position)x;
	    if (flag & YValue) w->core.y = (Position)y;
	    if (flag & WidthValue) w->core.width = (Dimension)width;
	    if (flag & HeightValue) w->core.height = (Dimension)height;
	}
	else {
	    String params[2];
	    Cardinal num_params = 2;
	    params[0] = XtName(W);
	    params[1] = w->shell.geometry;
	    XtAppWarningMsg(XtWidgetToApplicationContext(W),
       "badGeometry", "shellRealize", XtCXtToolkitError,
       "Shell widget \"%s\" has an invalid geometry specification: \"%s\"",
			    params, &num_params);
	}
    }
    else
	flag = 0;

    if (is_wmshell) {
	WMShellWidget wmshell = (WMShellWidget) w;
	if (wmshell->wm.win_gravity == XtUnspecifiedShellInt) {
	    if (win_gravity != -1)
		wmshell->wm.win_gravity = win_gravity;
	    else
		wmshell->wm.win_gravity = NorthWestGravity;
	}
	wmshell->wm.size_hints.flags |= PWinGravity;
	if ((flag & (XValue|YValue)) == (XValue|YValue))
	    wmshell->wm.size_hints.flags |= USPosition;
	if ((flag & (WidthValue|HeightValue)) == (WidthValue|HeightValue))
	    wmshell->wm.size_hints.flags |= USSize;
    }
    w->shell.client_specified |= _XtShellGeometryParsed;
}


static void ChangeManaged(Widget wid)
{
    ShellWidget w = (ShellWidget) wid;
    Widget child = NULL;
    Cardinal i;

    for (i = 0; i < w->composite.num_children; i++) {
	if (XtIsManaged(w->composite.children[i])) {
	    child = w->composite.children[i];
	    break;		/* there can only be one of them! */
	}
    }

    if (!XtIsRealized (wid))	/* then we're about to be realized... */
	GetGeometry(wid, child);

    if (child != NULL)
	XtConfigureWidget (child, (Position)0, (Position)0,
			   w->core.width, w->core.height, (Dimension)0 );
}

/*
 * This is gross, I can't wait to see if the change happened so I will ask
 * the window manager to change my size and do the appropriate X work.
 * I will then tell the requester that he can.  Care must be taken because
 * it is possible that some time in the future the request will be
 * asynchronusly denied and the window reverted to it's old size/shape.
 */

/*ARGSUSED*/
static XtGeometryResult GeometryManager(
	Widget wid,
	XtWidgetGeometry *request,
	XtWidgetGeometry *reply)
{
	ShellWidget shell = (ShellWidget)(wid->core.parent);
	XtWidgetGeometry my_request;

	if(shell->shell.allow_shell_resize == FALSE && XtIsRealized(wid))
		return(XtGeometryNo);

	if (request->request_mode & (CWX | CWY))
	    return(XtGeometryNo);

	my_request.request_mode = (request->request_mode & XtCWQueryOnly);
	if (request->request_mode & CWWidth) {
	    my_request.width = request->width;
	    my_request.request_mode |= CWWidth;
	}
	if (request->request_mode & CWHeight) {
	    my_request.height = request->height;
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

	    if (!(request->request_mode & XtCWQueryOnly)) {
		wid->core.width = shell->core.width;
		wid->core.height = shell->core.height;
		if (request->request_mode & CWBorderWidth) {
		    wid->core.x = wid->core.y = -request->border_width;
		}
	    }
	    return XtGeometryYes;
	} else return XtGeometryNo;
}

typedef struct {
	Widget  w;
	unsigned long request_num;
	Boolean done;
} QueryStruct;

static Bool isMine(
	Display *dpy,
	register XEvent  *event,
	char *arg)
{
	QueryStruct *q = (QueryStruct *) arg;
	register Widget w = q->w;

	if ( (dpy != XtDisplay(w)) || (event->xany.window != XtWindow(w)) ) {
	    return FALSE;
	}
	if (event->xany.serial >= q->request_num) {
	    if (event->type == ConfigureNotify) {
		q->done = TRUE;
		return TRUE;
	    }
	}
	else if (event->type == ConfigureNotify)
	    return TRUE;	/* flush old events */
	if (event->type == ReparentNotify
		 && event->xreparent.window == XtWindow(w)) {
	    /* we might get ahead of this event, so just in case someone
	     * asks for coordinates before this event is dispatched...
	     */
	    register ShellWidget s = (ShellWidget)w;
	    if (event->xreparent.parent != RootWindowOfScreen(XtScreen(w)))
		s->shell.client_specified &= ~_XtShellNotReparented;
	    else
		s->shell.client_specified |= _XtShellNotReparented;
	}
	return FALSE;
}

static Boolean _wait_for_response(
	ShellWidget	w,
	XEvent		*event,
        unsigned long	request_num)
{
	XtAppContext app = XtWidgetToApplicationContext((Widget) w);
	QueryStruct q;
	unsigned long timeout;

	if (XtIsWMShell((Widget)w))
	    timeout = ((WMShellWidget)w)->wm.wm_timeout;
	else
	    timeout = DEFAULT_WM_TIMEOUT;

	XFlush(XtDisplay(w));
	q.w = (Widget) w;
	q.request_num = request_num;
	q.done = FALSE;

	/*
	 * look for match event and discard all prior configures
	 */
	while (XCheckIfEvent(XtDisplay(w),event,isMine,(char*)&q)) {
	    if (q.done) return TRUE;
	}

	while (timeout > 0) {
	    if (_XtWaitForSomething (app,
				     FALSE, TRUE, TRUE, TRUE,
				     TRUE,
#ifdef XTHREADS
				     FALSE,
#endif
				     &timeout) != -1) {
		while (XCheckIfEvent(XtDisplay(w),event,isMine,(char*)&q)) {
		    if (q.done) return TRUE;
		}
	    }
	}
	return FALSE;
}

/*ARGSUSED*/
static XtGeometryResult RootGeometryManager(
    Widget gw,
    XtWidgetGeometry *request, XtWidgetGeometry *reply)
{
    register ShellWidget w = (ShellWidget)gw;
    XWindowChanges values;
    unsigned int mask = request->request_mode;
    XEvent event;
    Boolean wm;
    register struct _OldXSizeHints *hintp = NULL;
    int oldx, oldy, oldwidth, oldheight, oldborder_width;
    unsigned long request_num;

    CALLGEOTAT(_XtGeoTab(1));

    if (XtIsWMShell(gw)) {
	wm = True;
	hintp = &((WMShellWidget)w)->wm.size_hints;
	/* for draft-ICCCM wm's, need to make sure hints reflect
	   (current) reality so client can move and size separately. */
  	hintp->x = w->core.x;
  	hintp->y = w->core.y;
  	hintp->width = w->core.width;
   	hintp->height = w->core.height;
    } else
	wm = False;

    oldx = w->core.x;
    oldy = w->core.y;
    oldwidth = w->core.width;
    oldheight = w->core.height;
    oldborder_width = w->core.border_width;

#define PutBackGeometry() \
	{ w->core.x = oldx; \
	  w->core.y = oldy; \
	  w->core.width = oldwidth; \
	  w->core.height = oldheight; \
	  w->core.border_width = oldborder_width; }

    if (mask & CWX) {
	    if (w->core.x == request->x) mask &= ~CWX;
	    else {
		w->core.x = values.x = request->x;
		if (wm) {
		    hintp->flags &= ~USPosition;
		    hintp->flags |= PPosition;
		    hintp->x = values.x;
		}
	    }
    }
    if (mask & CWY) {
	    if (w->core.y == request->y) mask &= ~CWY;
	    else {
		w->core.y = values.y = request->y;
		if (wm) {
		    hintp->flags &= ~USPosition;
		    hintp->flags |= PPosition;
		    hintp->y = values.y;
		}
	    }
    }
    if (mask & CWBorderWidth) {
	    if (w->core.border_width == request->border_width) {
		    mask &= ~CWBorderWidth;
	    } else
		w->core.border_width =
		    values.border_width =
			request->border_width;
    }
    if (mask & CWWidth) {
	    if (w->core.width == request->width) mask &= ~CWWidth;
	    else {
		w->core.width = values.width = request->width;
		if (wm) {
		    hintp->flags &= ~USSize;
		    hintp->flags |= PSize;
		    hintp->width = values.width;
		}
	    }
    }
    if (mask & CWHeight) {
	    if (w->core.height == request->height) mask &= ~CWHeight;
	    else {
		w->core.height = values.height = request->height;
		if (wm) {
		    hintp->flags &= ~USSize;
		    hintp->flags |= PSize;
		    hintp->height = values.height;
		}
	    }
    }
    if (mask & CWStackMode) {
	values.stack_mode = request->stack_mode;
	if (mask & CWSibling)
	    values.sibling = XtWindow(request->sibling);
    }

    if (!XtIsRealized((Widget)w)) {
	CALLGEOTAT(_XtGeoTrace((Widget)w,
		      "Shell \"%s\" is not realized, return XtGeometryYes.\n",
		       XtName((Widget)w)));
    	CALLGEOTAT(_XtGeoTab(-1));
	return XtGeometryYes;
    }

    request_num = NextRequest(XtDisplay(w));

    CALLGEOTAT(_XtGeoTrace((Widget)w,"XConfiguring the Shell X window :\n"));
    CALLGEOTAT(_XtGeoTab(1));
#ifdef XT_GEO_TATTLER
    if (mask & CWX) { CALLGEOTAT(_XtGeoTrace((Widget)w,"x = %d\n",values.x));}
    if (mask & CWY) { CALLGEOTAT(_XtGeoTrace((Widget)w,"y = %d\n",values.y));}
    if (mask & CWWidth) { CALLGEOTAT(_XtGeoTrace((Widget)w,
					       "width = %d\n",values.width));}
    if (mask & CWHeight) { CALLGEOTAT(_XtGeoTrace((Widget)w,
					    "height = %d\n",values.height));}
    if (mask & CWBorderWidth) { CALLGEOTAT(_XtGeoTrace((Widget)w,
				  "border_width = %d\n",values.border_width));}
#endif
    CALLGEOTAT(_XtGeoTab(-1));

    XConfigureWindow(XtDisplay((Widget)w), XtWindow((Widget)w), mask,&values);

    if (wm && !w->shell.override_redirect
	&& mask & (CWX | CWY | CWWidth | CWHeight | CWBorderWidth)) {
	_SetWMSizeHints((WMShellWidget)w);
    }

    if (w->shell.override_redirect) {
	CALLGEOTAT(_XtGeoTrace((Widget)w,"Shell \"%s\" is override redirect, return XtGeometryYes.\n", XtName((Widget)w)));
    	CALLGEOTAT(_XtGeoTab(-1));
	return XtGeometryYes;
    }


    /* If no non-stacking bits are set, there's no way to tell whether
       or not this worked, so assume it did */

    if (!(mask & ~(CWStackMode | CWSibling))) return XtGeometryYes;

    if (wm && ((WMShellWidget)w)->wm.wait_for_wm == FALSE) {
	    /* the window manager is sick
	     * so I will do the work and
	     * say no so if a new WM starts up,
	     * or the current one recovers
	     * my size requests will be visible
	     */
	CALLGEOTAT(_XtGeoTrace((Widget)w,"Shell \"%s\" has wait_for_wm == FALSE, return XtGeometryNo.\n",
		       XtName((Widget)w)));
    	CALLGEOTAT(_XtGeoTab(-1));

	PutBackGeometry();
	return XtGeometryNo;
    }

    if (_wait_for_response(w, &event, request_num)) {
	/* got an event */
	if (event.type == ConfigureNotify) {

#define NEQ(x, msk) ((mask & msk) && (values.x != event.xconfigure.x))
	    if (NEQ(x, CWX) ||
		NEQ(y, CWY) ||
		NEQ(width, CWWidth) ||
		NEQ(height, CWHeight) ||
		NEQ(border_width, CWBorderWidth)) {
#ifdef XT_GEO_TATTLER
		if (NEQ(x, CWX)) {
		    CALLGEOTAT(_XtGeoTrace((Widget)w,
					   "received Configure X %d\n",
					   event.xconfigure.x));
		}
		if (NEQ(y, CWY)) {
		    CALLGEOTAT(_XtGeoTrace((Widget)w,
					   "received Configure Y %d\n",
					   event.xconfigure.y));
		}
		if (NEQ(width, CWWidth)) {
		    CALLGEOTAT(_XtGeoTrace((Widget)w,
					   "received Configure Width %d\n",
					   event.xconfigure.width));
		}
		if (NEQ(height, CWHeight)) {
		    CALLGEOTAT(_XtGeoTrace((Widget)w,
					   "received Configure Height %d\n",
					   event.xconfigure.height));
		}
		if (NEQ(border_width, CWBorderWidth)) {
		    CALLGEOTAT(_XtGeoTrace((Widget)w,
				        "received Configure BorderWidth %d\n",
					event.xconfigure.border_width));
		}
#endif
#undef NEQ
		XPutBackEvent(XtDisplay(w), &event);
		PutBackGeometry();
		/*
		 * We just potentially re-ordered the event queue
		 * w.r.t. ConfigureNotifies with some trepidation.
		 * But this is probably a Good Thing because we
		 * will know the new true state of the world sooner
		 * this way.
		 */
		CALLGEOTAT(_XtGeoTrace((Widget)w,
			   "ConfigureNotify failed, return XtGeometryNo.\n"));
		CALLGEOTAT(_XtGeoTab(-1));

		return XtGeometryNo;
	    }
	    else {
		w->core.width = event.xconfigure.width;
		w->core.height = event.xconfigure.height;
		w->core.border_width = event.xconfigure.border_width;
		if (event.xany.send_event || /* ICCCM compliant synth */
		    w->shell.client_specified & _XtShellNotReparented) {

		    w->core.x = event.xconfigure.x;
		    w->core.y = event.xconfigure.y;
		    w->shell.client_specified |= _XtShellPositionValid;
		}
		else w->shell.client_specified &= ~_XtShellPositionValid;
		CALLGEOTAT(_XtGeoTrace((Widget)w,
			 "ConfigureNotify succeed, return XtGeometryYes.\n"));
		CALLGEOTAT(_XtGeoTab(-1));
		return XtGeometryYes;
	    }
	} else if (!wm) {
	    PutBackGeometry();
	    CALLGEOTAT(_XtGeoTrace((Widget)w,
				   "Not wm, return XtGeometryNo.\n"));
	    CALLGEOTAT(_XtGeoTab(-1));
	    return XtGeometryNo;
	} else XtAppWarningMsg(XtWidgetToApplicationContext((Widget)w),
			       "internalError", "shell", XtCXtToolkitError,
			       "Shell's window manager interaction is broken",
			       (String *)NULL, (Cardinal *)NULL);
    } else if (wm) { /* no event */
	((WMShellWidget)w)->wm.wait_for_wm = FALSE; /* timed out; must be broken */
    }
    PutBackGeometry();
#undef PutBackGeometry
    CALLGEOTAT(_XtGeoTrace((Widget)w,
			   "Timeout passed?, return XtGeometryNo.\n"));
    CALLGEOTAT(_XtGeoTab(-1));
    return XtGeometryNo;
		}

/* ARGSUSED */
static Boolean SetValues(
	Widget old, Widget ref, Widget new,
	ArgList args,
	Cardinal *num_args)
{
	ShellWidget nw = (ShellWidget) new;
	ShellWidget ow = (ShellWidget) old;
	Mask mask = 0;
	XSetWindowAttributes attr;

	if (!XtIsRealized(new))
	    return False;

	if (ow->shell.save_under != nw->shell.save_under) {
	    mask = CWSaveUnder;
	    attr.save_under = nw->shell.save_under;
	}

	if (ow->shell.override_redirect != nw->shell.override_redirect) {
	    mask |= CWOverrideRedirect;
	    attr.override_redirect = nw->shell.override_redirect;
	}

	if (mask) {
	    XChangeWindowAttributes(XtDisplay(new),XtWindow(new), mask, &attr);
	    if ((mask & CWOverrideRedirect) && !nw->shell.override_redirect)
		_popup_set_prop(nw);
	}

	if (! (ow->shell.client_specified & _XtShellPositionValid)) {
	    Cardinal n;

	    for (n = *num_args; n; n--, args++) {
		if (strcmp(XtNx, args->name) == 0) {
		    _XtShellGetCoordinates((Widget)ow, &ow->core.x,
							&ow->core.y);
		} else if (strcmp(XtNy, args->name) == 0) {
		    _XtShellGetCoordinates((Widget)ow, &ow->core.x,
							&ow->core.y);
		}
	    }
	}
	return FALSE;
}

/* ARGSUSED */
static Boolean WMSetValues(
	Widget old, Widget ref, Widget new,
	ArgList args,		/* unused */
	Cardinal *num_args)	/* unused */
{
	WMShellWidget nwmshell = (WMShellWidget) new;
	WMShellWidget owmshell = (WMShellWidget) old;
	Boolean set_prop
	    = XtIsRealized(new) && !nwmshell->shell.override_redirect;
	Boolean title_changed;

	EvaluateSizeHints(nwmshell);

#define NEQ(f) (nwmshell->wm.size_hints.f != owmshell->wm.size_hints.f)

	if (set_prop
	    && (NEQ(flags) || NEQ(min_width) || NEQ(min_height)
		|| NEQ(max_width) || NEQ(max_height)
		|| NEQ(width_inc) || NEQ(height_inc)
		|| NEQ(min_aspect.x) || NEQ(min_aspect.y)
		|| NEQ(max_aspect.x) || NEQ(max_aspect.y)
#undef NEQ
#define NEQ(f) (nwmshell->wm.f != owmshell->wm.f)

		|| NEQ(base_width) || NEQ(base_height) || NEQ(win_gravity))) {
	    _SetWMSizeHints(nwmshell);
	}
#undef NEQ

	if (nwmshell->wm.title != owmshell->wm.title) {
	    XtFree(owmshell->wm.title);
	    if (! nwmshell->wm.title) nwmshell->wm.title = "";
	    nwmshell->wm.title = XtNewString(nwmshell->wm.title);
	    title_changed = True;
	} else
	    title_changed = False;

	if (set_prop
	    && (title_changed ||
		nwmshell->wm.title_encoding != owmshell->wm.title_encoding)) {

	    XTextProperty title;
	    Boolean copied = False;

            if (nwmshell->wm.title_encoding == None &&
		XmbTextListToTextProperty(XtDisplay(new),
					  (char**)&nwmshell->wm.title,
					  1, XStdICCTextStyle,
					  &title) >= Success) {
		copied = True;
	    } else {
		title.value = (unsigned char*)nwmshell->wm.title;
		title.encoding = nwmshell->wm.title_encoding ?
		    nwmshell->wm.title_encoding : XA_STRING;
		title.format = 8;
		title.nitems = strlen(nwmshell->wm.title);
	    }
	    XSetWMName(XtDisplay(new), XtWindow(new), &title);
	    if (copied)
		XFree((XPointer)title.value);
	}

	EvaluateWMHints(nwmshell);

#define NEQ(f)	(nwmshell->wm.wm_hints.f != owmshell->wm.wm_hints.f)

	if (set_prop
	    && (NEQ(flags) || NEQ(input) || NEQ(initial_state)
		|| NEQ(icon_x) || NEQ(icon_y)
		|| NEQ(icon_pixmap) || NEQ(icon_mask) || NEQ(icon_window)
		|| NEQ(window_group))) {

	    XSetWMHints(XtDisplay(new), XtWindow(new), &nwmshell->wm.wm_hints);
	}
#undef NEQ

 	if (XtIsRealized(new) &&
	    nwmshell->wm.transient != owmshell->wm.transient) {
 	    if (nwmshell->wm.transient) {
		if (!XtIsTransientShell(new) &&
		    !nwmshell->shell.override_redirect &&
		    nwmshell->wm.wm_hints.window_group !=
		       XtUnspecifiedWindowGroup)
		    XSetTransientForHint(XtDisplay(new), XtWindow(new),
					 nwmshell->wm.wm_hints.window_group);
	    }
 	    else XDeleteProperty(XtDisplay(new), XtWindow(new),
 				 XA_WM_TRANSIENT_FOR);
 	}

	if (nwmshell->wm.client_leader != owmshell->wm.client_leader
	    && XtWindow(new) && !nwmshell->shell.override_redirect) {
	    Widget leader = GetClientLeader(new);
	    if (XtWindow(leader))
		XChangeProperty(XtDisplay(new), XtWindow(new),
				XInternAtom(XtDisplay(new),
					    "WM_CLIENT_LEADER", False),
				XA_WINDOW, 32, PropModeReplace,
				(unsigned char *) &(leader->core.window), 1);
	}

	if (nwmshell->wm.window_role != owmshell->wm.window_role) {
	    XtFree(owmshell->wm.window_role);
	    if (set_prop && nwmshell->wm.window_role) {
		XChangeProperty(XtDisplay(new), XtWindow(new),
				XInternAtom(XtDisplay(new), "WM_WINDOW_ROLE",
					    False),
				XA_STRING, 8, PropModeReplace,
				(unsigned char *)nwmshell->wm.window_role,
				strlen(nwmshell->wm.window_role));
	    } else if (XtIsRealized(new) && ! nwmshell->wm.window_role) {
		XDeleteProperty(XtDisplay(new), XtWindow(new),
				XInternAtom(XtDisplay(new), "WM_WINDOW_ROLE",
					    False));
	    }
	}

	return FALSE;
}

/*ARGSUSED*/
static Boolean TransientSetValues(
     Widget oldW, Widget refW, Widget newW,
     ArgList args,		/* unused */
     Cardinal *num_args)	/* unused */
{
    TransientShellWidget old = (TransientShellWidget)oldW;
    TransientShellWidget new = (TransientShellWidget)newW;

    if (XtIsRealized(newW)
	&& ((new->wm.transient && !old->wm.transient)
	    || ((new->transient.transient_for != old->transient.transient_for)
		|| (new->transient.transient_for == NULL
		    && (new->wm.wm_hints.window_group
			!= old->wm.wm_hints.window_group))))) {

	_SetTransientForHint(new, True);
    }
    return False;
}


/* ARGSUSED */
static Boolean TopLevelSetValues(
     Widget oldW, Widget refW, Widget newW,
     ArgList args,		/* unused */
     Cardinal *num_args)	/* unused */
{
    TopLevelShellWidget old = (TopLevelShellWidget)oldW;
    TopLevelShellWidget new = (TopLevelShellWidget)newW;
    Boolean name_changed;

    if (old->topLevel.icon_name != new->topLevel.icon_name) {
	XtFree((XtPointer)old->topLevel.icon_name);
	if (! new->topLevel.icon_name) new->topLevel.icon_name = "";
	new->topLevel.icon_name = XtNewString(new->topLevel.icon_name);
	name_changed = True;
    } else
	name_changed = False;

    if (XtIsRealized(newW)) {
	if (new->topLevel.iconic != old->topLevel.iconic) {
	    if (new->topLevel.iconic)
		XIconifyWindow(XtDisplay(newW),
			       XtWindow(newW),
			       XScreenNumberOfScreen(XtScreen(newW))
			       );
	    else {
		Boolean map = new->shell.popped_up;
		XtPopup(newW, XtGrabNone);
		if (map) XMapWindow(XtDisplay(newW), XtWindow(newW));
	    }
	}

	if (!new->shell.override_redirect &&
	    (name_changed ||
	     (old->topLevel.icon_name_encoding
	      != new->topLevel.icon_name_encoding))) {

	    XTextProperty icon_name;
	    Boolean copied = False;

            if (new->topLevel.icon_name_encoding == None &&
		XmbTextListToTextProperty(XtDisplay(newW),
					  (char**) &new->topLevel.icon_name,
					  1, XStdICCTextStyle,
					  &icon_name) >= Success) {
		copied = True;
	    } else {
		icon_name.value = (unsigned char *)new->topLevel.icon_name;
		icon_name.encoding = new->topLevel.icon_name_encoding ?
		    new->topLevel.icon_name_encoding : XA_STRING;
		icon_name.format = 8;
		icon_name.nitems = strlen((char *)icon_name.value);
	    }
	    XSetWMIconName(XtDisplay(newW), XtWindow(newW), &icon_name);
	    if (copied)
		XFree((XPointer)icon_name.value);
	}
    }
    else if (new->topLevel.iconic != old->topLevel.iconic) {
	if (new->topLevel.iconic)
	    new->wm.wm_hints.initial_state = IconicState;
    }
    return False;
}

static String * NewArgv(
    int count,
    String *str)  /* do not assume it's terminated by a NULL element */
{
    Cardinal nbytes = 0;
    Cardinal num = 0;
    String *newarray, *new;
    String *strarray = str;
    String sptr;

    if (count <= 0 || !str) return NULL;

    for (num = count; num--; str++) {
	nbytes += strlen(*str);
	nbytes++;
    }
    num = (count+1) * sizeof(String);
    new = newarray = (String *) __XtMalloc(num + nbytes);
    sptr = ((char *) new) + num;

    for (str = strarray; count--; str++) {
	*new = sptr;
	strcpy(*new, *str);
	new++;
	sptr = strchr(sptr, '\0');
	sptr++;
    }
    *new = NULL;
    return newarray;
}


/*ARGSUSED*/
static Boolean ApplicationSetValues(
    Widget current, Widget request, Widget new,
    ArgList args,
    Cardinal *num_args)
{
    ApplicationShellWidget nw = (ApplicationShellWidget) new;
    ApplicationShellWidget cw = (ApplicationShellWidget) current;

    if (cw->application.argc != nw->application.argc ||
	cw->application.argv != nw->application.argv) {

	if (nw->application.argc > 0)
	    nw->application.argv = NewArgv(nw->application.argc,
					   nw->application.argv);
	if (cw->application.argc > 0)
	    FreeStringArray(cw->application.argv);

	if (XtIsRealized(new) && !nw->shell.override_redirect) {
	    if (nw->application.argc >= 0 && nw->application.argv)
		XSetCommand(XtDisplay(new), XtWindow(new),
			    nw->application.argv, nw->application.argc);
	    else
		XDeleteProperty(XtDisplay(new), XtWindow(new), XA_WM_COMMAND);
	}
    }
    return False;
}

/*ARGSUSED*/
static Boolean SessionSetValues(
    Widget current, Widget request, Widget new,
    ArgList args,
    Cardinal *num_args)
{
#ifndef XT_NO_SM
    SessionShellWidget nw = (SessionShellWidget) new;
    SessionShellWidget cw = (SessionShellWidget) current;
    unsigned long set_mask = 0UL;
    unsigned long unset_mask = 0UL;
    Boolean initialize = False;

    if (cw->session.session_id != nw->session.session_id) {
	nw->session.session_id = XtNewString(nw->session.session_id);
	XtFree(cw->session.session_id);
    }

    if (cw->session.clone_command != nw->session.clone_command) {
	if (nw->session.clone_command) {
	    nw->session.clone_command =
		NewStringArray(nw->session.clone_command);
	    set_mask |= XtCloneCommandMask;
	} else unset_mask |= XtCloneCommandMask;
	FreeStringArray(cw->session.clone_command);
    }

    if (cw->session.current_dir != nw->session.current_dir) {
	if (nw->session.current_dir) {
	    nw->session.current_dir =
		XtNewString(nw->session.current_dir);
	    set_mask |= XtCurrentDirectoryMask;
	} else unset_mask |= XtCurrentDirectoryMask;
	XtFree((char *) cw->session.current_dir);
    }

    if (cw->session.discard_command != nw->session.discard_command) {
	if (nw->session.discard_command) {
	    nw->session.discard_command =
		NewStringArray(nw->session.discard_command);
	    set_mask |=  XtDiscardCommandMask;
	} else unset_mask |= XtDiscardCommandMask;
	FreeStringArray(cw->session.discard_command);
    }

    if (cw->session.environment != nw->session.environment) {
	if (nw->session.environment) {
	    nw->session.environment =
		NewStringArray(nw->session.environment);
	    set_mask |= XtEnvironmentMask;
	} else unset_mask |= XtEnvironmentMask;
	FreeStringArray(cw->session.environment);
    }

    if (cw->session.program_path != nw->session.program_path) {
	if (nw->session.program_path) {
	    nw->session.program_path =
		XtNewString(nw->session.program_path);
	    set_mask |= XtProgramMask;
	} else unset_mask |= XtProgramMask;
	XtFree((char *) cw->session.program_path);
    }

    if (cw->session.resign_command != nw->session.resign_command) {
	if (nw->session.resign_command) {
	    nw->session.resign_command =
		NewStringArray(nw->session.resign_command);
	    set_mask |= XtResignCommandMask;
	} else set_mask |= XtResignCommandMask;
	FreeStringArray(cw->session.resign_command);
    }

    if (cw->session.restart_command != nw->session.restart_command) {
	if (nw->session.restart_command) {
	    nw->session.restart_command =
		NewStringArray(nw->session.restart_command);
	    set_mask |= XtRestartCommandMask;
	} else unset_mask |= XtRestartCommandMask;
	FreeStringArray(cw->session.restart_command);
    }

    if (cw->session.restart_style != nw->session.restart_style)
	set_mask |= XtRestartStyleHintMask;

    if (cw->session.shutdown_command != nw->session.shutdown_command) {
	if (nw->session.shutdown_command) {
	    nw->session.shutdown_command =
		NewStringArray(nw->session.shutdown_command);
	    set_mask |= XtShutdownCommandMask;
	} else unset_mask |= XtShutdownCommandMask;
	FreeStringArray(cw->session.shutdown_command);
    }

    if ((!cw->session.join_session && nw->session.join_session) ||
	(!cw->session.connection && nw->session.connection)) {
	JoinSession(nw);
	initialize = True;
    }

    if (nw->session.connection && (set_mask || unset_mask || initialize))
	SetSessionProperties((SessionShellWidget) new, initialize, set_mask, unset_mask);

    if ((cw->session.join_session && !nw->session.join_session) ||
	(cw->session.connection && !nw->session.connection))
	StopManagingSession(nw, nw->session.connection);
#endif /* !XT_NO_SM */

    if (cw->wm.client_leader != nw->wm.client_leader ||
	cw->session.session_id != nw->session.session_id) {
	Widget leader;
	if (cw->session.session_id) {
	    leader = GetClientLeader(current);
	    if (XtWindow(leader))
		XDeleteProperty(XtDisplay(leader), XtWindow(leader),
				XInternAtom(XtDisplay(leader), "SM_CLIENT_ID",
					    False));
	}
	if (nw->session.session_id) {
	    leader = GetClientLeader(new);
	    if (XtWindow(leader))
		XChangeProperty(XtDisplay(leader), XtWindow(leader),
				XInternAtom(XtDisplay(leader), "SM_CLIENT_ID",
					    False),
				XA_STRING, 8, PropModeReplace,
				(unsigned char *) nw->session.session_id,
				strlen(nw->session.session_id));
	}
    }
    return False;
}

void _XtShellGetCoordinates(
    Widget widget,
    Position* x,
    Position* y)
{
    ShellWidget w = (ShellWidget)widget;
    if (XtIsRealized(widget) &&
	!(w->shell.client_specified & _XtShellPositionValid)) {
	int tmpx, tmpy;
	Window tmpchild;
	(void) XTranslateCoordinates(XtDisplay(w), XtWindow(w),
				     RootWindowOfScreen(XtScreen(w)),
				     (int) -w->core.border_width,
				     (int) -w->core.border_width,
				     &tmpx, &tmpy, &tmpchild);
	w->core.x = tmpx;
	w->core.y = tmpy;
	w->shell.client_specified |= _XtShellPositionValid;
    }
    *x = w->core.x;
    *y = w->core.y;
}

static void GetValuesHook(
    Widget	widget,
    ArgList	args,
    Cardinal*	num_args)
{
    ShellWidget w = (ShellWidget) widget;

    /* x and y resource values may be invalid after a shell resize */
    if (XtIsRealized(widget) &&
	!(w->shell.client_specified & _XtShellPositionValid)) {
	Cardinal	n;
	Position	x, y;

	for (n = *num_args; n; n--, args++) {
	    if (strcmp(XtNx, args->name) == 0) {
		_XtShellGetCoordinates(widget, &x, &y);
		_XtCopyToArg((char *) &x, &args->value, sizeof(Position));
	    } else if (strcmp(XtNy, args->name) == 0) {
		_XtShellGetCoordinates(widget, &x, &y);
		_XtCopyToArg((char *) &y, &args->value, sizeof(Position));
	    }
	}
    }
}

static void ApplicationShellInsertChild(
    Widget widget)
{
    if (! XtIsWidget(widget) && XtIsRectObj(widget)) {
	XtAppWarningMsg(XtWidgetToApplicationContext(widget),
	       "invalidClass", "applicationShellInsertChild", XtCXtToolkitError,
	       "ApplicationShell does not accept RectObj children; ignored",
	       (String*)NULL, (Cardinal*)NULL);
    }
    else {
	XtWidgetProc insert_child;

	LOCK_PROCESS;
	insert_child =
	    ((CompositeWidgetClass)applicationShellClassRec.core_class.
	   superclass)->composite_class.insert_child;
	UNLOCK_PROCESS;
	(*insert_child) (widget);
    }
}

/**************************************************************************

  Session Protocol Participation

 *************************************************************************/

#define XtSessionCheckpoint	0
#define XtSessionInteract	1

static void CallSaveCallbacks(SessionShellWidget );
static String *EditCommand(String, String *, String *);
static Boolean ExamineToken(XtPointer);
static void GetIceEvent(XtPointer, int *, XtInputId *);
static XtCheckpointToken GetToken(Widget, int);
static void XtCallCancelCallbacks(SmcConn, SmPointer);
static void XtCallDieCallbacks(SmcConn, SmPointer);
static void XtCallSaveCallbacks(SmcConn, SmPointer, int, Bool, int, Bool);
static void XtCallSaveCompleteCallbacks(SmcConn, SmPointer);

#ifndef XT_NO_SM
static void StopManagingSession(
    SessionShellWidget w,
    SmcConn connection) /* connection to close, if any */
{
    if (connection)
	SmcCloseConnection(connection, 0, NULL);

    if (w->session.input_id) {
	XtRemoveInput(w->session.input_id);
	w->session.input_id = 0;
    }
    w->session.connection = NULL;
}

#define XT_MSG_LENGTH 256
static void JoinSession(
    SessionShellWidget w)
{
    IceConn ice_conn;
    SmcCallbacks smcb;
    char * sm_client_id;
    unsigned long mask;
    static char context;  /* used to guarantee the connection isn't shared */

    smcb.save_yourself.callback = XtCallSaveCallbacks;
    smcb.die.callback = XtCallDieCallbacks;
    smcb.save_complete.callback = XtCallSaveCompleteCallbacks;
    smcb.shutdown_cancelled.callback = XtCallCancelCallbacks;
    smcb.save_yourself.client_data = smcb.die.client_data =
	smcb.save_complete.client_data =
	    smcb.shutdown_cancelled.client_data = (SmPointer) w;
    mask = SmcSaveYourselfProcMask | SmcDieProcMask |
	SmcSaveCompleteProcMask | SmcShutdownCancelledProcMask;

    if (w->session.connection) {
	SmcModifyCallbacks(w->session.connection, mask, &smcb);
	sm_client_id = SmcClientID(w->session.connection);
    } else if (getenv("SESSION_MANAGER")) {
	char error_msg[XT_MSG_LENGTH];
	error_msg[0] = '\0';
	w->session.connection =
	    SmcOpenConnection(NULL, &context, SmProtoMajor, SmProtoMinor,
			      mask, &smcb, w->session.session_id,
			      &sm_client_id, XT_MSG_LENGTH, error_msg);
	if (error_msg[0]) {
	    String params[1];
	    Cardinal num_params = 1;
	    params[0] = error_msg;
	    XtAppWarningMsg(XtWidgetToApplicationContext((Widget) w),
			    "sessionManagement", "SmcOpenConnection",
			    XtCXtToolkitError,
			    "Tried to connect to session manager, %s",
			    params, &num_params);
	}
    }

    if (w->session.connection) {
	if (w->session.session_id == NULL
	    || (strcmp(w->session.session_id, sm_client_id) != 0)) {
	    XtFree(w->session.session_id);
	    w->session.session_id = XtNewString(sm_client_id);
	}
	free(sm_client_id);
	ice_conn = SmcGetIceConnection(w->session.connection);
	w->session.input_id =
	    XtAppAddInput(XtWidgetToApplicationContext((Widget)w),
			  IceConnectionNumber(ice_conn),
			  (XtPointer) XtInputReadMask,
			  GetIceEvent, (XtPointer) w);

	w->session.restart_command =
	    EditCommand(w->session.session_id, w->session.restart_command,
			w->application.argv);

	if (! w->session.clone_command) w->session.clone_command =
	    EditCommand(NULL, NULL, w->session.restart_command);

	if (! w->session.program_path)
	    w->session.program_path = w->session.restart_command
		? XtNewString(w->session.restart_command[0]) : NULL;
    }
}
#undef XT_MSG_LENGTH

#endif /* !XT_NO_SM */

static String * NewStringArray(String *str)
{
    Cardinal nbytes = 0;
    Cardinal num = 0;
    String *newarray, *new;
    String *strarray = str;
    String sptr;

    if (!str) return NULL;

    for (num = 0; *str; num++, str++) {
	nbytes += strlen(*str);
	nbytes++;
    }
    num = (num + 1) * sizeof(String);
    new = newarray = (String *) __XtMalloc(num + nbytes);
    sptr = ((char *) new) + num;

    for (str = strarray; *str; str++) {
	*new = sptr;
	strcpy(*new, *str);
	new++;
	sptr = strchr(sptr, '\0');
	sptr++;
    }
    *new = NULL;
    return newarray;
}

static void FreeStringArray(String *str)
{
    if (str)
	XtFree((char *) str);
}


#ifndef XT_NO_SM
static SmProp * CardPack(
    char *name,
    XtPointer closure)
{
    unsigned char *prop = (unsigned char *) closure;
    SmProp *p;

    p = (SmProp *) __XtMalloc(sizeof(SmProp) + sizeof(SmPropValue));
    p->vals = (SmPropValue *) (((char *) p) + sizeof(SmProp));
    p->num_vals = 1;
    p->type = SmCARD8;
    p->name = name;
    p->vals->length = 1;
    p->vals->value = (SmPointer) prop;
    return p;
}

static SmProp * ArrayPack(char *name, XtPointer closure)
{
    String prop = *(String *) closure;
    SmProp *p;

    p = (SmProp *) __XtMalloc(sizeof(SmProp) + sizeof(SmPropValue));
    p->vals = (SmPropValue *) (((char *) p) + sizeof(SmProp));
    p->num_vals = 1;
    p->type = SmARRAY8;
    p->name = name;
    p->vals->length = strlen(prop) + 1;
    p->vals->value = prop;
    return p;
}

static SmProp * ListPack(
    char *name,
    XtPointer closure)
{
    String *prop = *(String **) closure;
    SmProp *p;
    String *ptr;
    SmPropValue *vals;
    int n = 0;

    for (ptr = prop; *ptr; ptr++)
	n++;
    p = (SmProp*) __XtMalloc(sizeof(SmProp) + (Cardinal)(n*sizeof(SmPropValue)));
    p->vals = (SmPropValue *) (((char *) p) + sizeof(SmProp));
    p->num_vals = n;
    p->type = SmLISTofARRAY8;
    p->name = name;
    for (ptr = prop, vals = p->vals; *ptr; ptr++, vals++) {
	vals->length = strlen(*ptr) + 1;
	vals->value = *ptr;
    }
    return p;
}

static void FreePacks(
    SmProp **props,
    int num_props)
{
    while (--num_props >= 0)
	XtFree((char *) props[num_props]);
}

typedef SmProp* (*PackProc)(char *, XtPointer);

typedef struct PropertyRec {
    char *	name;
    int		offset;
    PackProc	proc;
} PropertyRec, *PropertyTable;

#define Offset(x) (XtOffsetOf(SessionShellRec, x))
static PropertyRec propertyTable[] = {
  {SmCloneCommand,     Offset(session.clone_command),    ListPack},
  {SmCurrentDirectory, Offset(session.current_dir),      ArrayPack},
  {SmDiscardCommand,   Offset(session.discard_command),  ListPack},
  {SmEnvironment,      Offset(session.environment),      ListPack},
  {SmProgram,          Offset(session.program_path),     ArrayPack},
  {SmResignCommand,    Offset(session.resign_command),   ListPack},
  {SmRestartCommand,   Offset(session.restart_command),  ListPack},
  {SmRestartStyleHint, Offset(session.restart_style),    CardPack},
  {SmShutdownCommand,  Offset(session.shutdown_command), ListPack}
};
#undef Offset

#define XT_NUM_SM_PROPS 11

static void SetSessionProperties(
    SessionShellWidget w,
    Boolean initialize,
    unsigned long set_mask,
    unsigned long unset_mask)
{
    PropertyTable p = propertyTable;
    int n;
    int num_props = 0;
    XtPointer *addr;
    unsigned long mask;
    SmProp *props[XT_NUM_SM_PROPS];
    char *pnames[XT_NUM_SM_PROPS];

    if (w->session.connection == NULL)
	return;

    if (initialize) {
	char nam_buf[32];
	char pid[12];
	String user_name;
	String pidp = pid;

	/* set all non-NULL session properties, the UserID and the ProcessID */
	for (n = XtNumber(propertyTable); n; n--, p++) {
	    addr = (XtPointer *) ((char *) w + p->offset);
	    if (p->proc == CardPack) {
		if (*(unsigned char *)addr)
		    props[num_props++] =(*(p->proc))(p->name, (XtPointer)addr);
	    }
	    else if (* addr)
		props[num_props++] = (*(p->proc))(p->name, (XtPointer)addr);

	}
	user_name = _XtGetUserName(nam_buf, sizeof nam_buf);
	if (user_name)
	    props[num_props++] = ArrayPack(SmUserID, &user_name);
	snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	props[num_props++] = ArrayPack(SmProcessID, &pidp);

	if (num_props) {
	    SmcSetProperties(w->session.connection, num_props, props);
	    FreePacks(props, num_props);
	}
	return;
    }

    if (set_mask) {
	mask = 1L;
	for (n = XtNumber(propertyTable); n; n--, p++, mask <<= 1)
	    if (mask & set_mask) {
		addr = (XtPointer *) ((char *) w + p->offset);
		props[num_props++] = (*(p->proc))(p->name, (XtPointer)addr);
	    }
	SmcSetProperties(w->session.connection, num_props, props);
	FreePacks(props, num_props);
    }

    if (unset_mask) {
	mask = 1L;
	num_props = 0;
	for (n = XtNumber(propertyTable); n; n--, p++, mask <<= 1)
	    if (mask & unset_mask)
		pnames[num_props++] = p->name;
	SmcDeleteProperties(w->session.connection, num_props, pnames);
    }
}

/*ARGSUSED*/
static void GetIceEvent(
    XtPointer	client_data,
    int *	source,
    XtInputId *	id)
{
    SessionShellWidget w = (SessionShellWidget) client_data;
    IceProcessMessagesStatus status;

    status = IceProcessMessages(SmcGetIceConnection(w->session.connection),
				NULL, NULL);

    if (status == IceProcessMessagesIOError) {
	StopManagingSession(w, w->session.connection);
	XtCallCallbackList((Widget)w, w->session.error_callbacks,
			   (XtPointer) NULL);
    }
}

static void CleanUpSave(
    SessionShellWidget w)
{
    XtSaveYourself next = w->session.save->next;
    XtFree((char *)w->session.save);
    w->session.save = next;
    if (w->session.save)
	CallSaveCallbacks(w);
}

static void CallSaveCallbacks(
    SessionShellWidget w)
{
    XtCheckpointToken token;

    if (XtHasCallbacks((Widget) w, XtNsaveCallback) != XtCallbackHasSome) {
	/* if the application makes no attempt to save state, report failure */
	SmcSaveYourselfDone(w->session.connection, False);
	CleanUpSave(w);
    } else {
	w->session.checkpoint_state = XtSaveActive;
	token = GetToken((Widget) w, XtSessionCheckpoint);
	_XtCallConditionalCallbackList((Widget)w, w->session.save_callbacks,
				       (XtPointer)token, ExamineToken);
	XtSessionReturnToken(token);
    }
}

/*ARGSUSED*/
static void XtCallSaveCallbacks(
    SmcConn	connection,	/* unused */
    SmPointer	client_data,
    int		save_type,
    Bool	shutdown,
    int		interact,
    Bool	fast)
{
    SessionShellWidget w = (SessionShellWidget) client_data;
    XtSaveYourself save;
    XtSaveYourself prev;

    save = XtNew(XtSaveYourselfRec);
    save->next = NULL;
    save->save_type = save_type;
    save->interact_style = interact;
    save->shutdown = shutdown;
    save->fast = fast;
    save->cancel_shutdown = False;
    save->phase = 1;
    save->interact_dialog_type = SmDialogNormal;
    save->request_cancel = save->request_next_phase = False;
    save->save_success = True;
    save->save_tokens = save->interact_tokens = 0;

    prev = (XtSaveYourself) &w->session.save;
    while (prev->next)
	prev = prev->next;
    prev->next = save;

    if (w->session.checkpoint_state == XtSaveInactive)
	CallSaveCallbacks(w);
}

static void XtInteractPermission(
    SmcConn	connection,
    SmPointer	data)
{
    Widget w = (Widget) data;
    SessionShellWidget sw = (SessionShellWidget) data;
    XtCheckpointToken token;
    XtCallbackProc callback;
    XtPointer client_data;


    _XtPeekCallback(w, sw->session.interact_callbacks, &callback,
		    &client_data);
    if (callback) {
	sw->session.checkpoint_state = XtInteractActive;
	token = GetToken(w, XtSessionInteract);
    	XtRemoveCallback(w, XtNinteractCallback, callback, client_data);
	(*callback)(w, client_data, (XtPointer) token);
    } else if (! sw->session.save->cancel_shutdown) {
	SmcInteractDone(connection, False);
    }
}

/*ARGSUSED*/
static void XtCallSaveCompleteCallbacks(
    SmcConn	connection,
    SmPointer	client_data)
{
    SessionShellWidget w =  (SessionShellWidget) client_data;

    XtCallCallbackList((Widget)w, w->session.save_complete_callbacks,
		       (XtPointer) NULL);
}

/*ARGSUSED*/
static void XtCallNextPhaseCallbacks(
    SmcConn	connection,	/* unused */
    SmPointer	client_data)
{
    SessionShellWidget w =  (SessionShellWidget) client_data;
    w->session.save->phase = 2;
    CallSaveCallbacks(w);
}

/*ARGSUSED*/
static void XtCallDieCallbacks(
    SmcConn	connection,	/* unused */
    SmPointer	client_data)
{
    SessionShellWidget w =  (SessionShellWidget) client_data;

    StopManagingSession(w, w->session.connection);
    XtCallCallbackList((Widget)w, w->session.die_callbacks,
		       (XtPointer) NULL);
}

/*ARGSUSED*/
static void XtCallCancelCallbacks(
    SmcConn	connection,	/* unused */
    SmPointer	client_data)
{
    SessionShellWidget w = (SessionShellWidget) client_data;
    Boolean call_interacts = False;

    if (w->session.checkpoint_state != XtSaveInactive) {
	w->session.save->cancel_shutdown = True;
	call_interacts = (w->session.save->interact_style !=
			  SmInteractStyleNone);
    }

    XtCallCallbackList((Widget)w, w->session.cancel_callbacks,
		       (XtPointer) NULL);

    if (call_interacts) {
	w->session.save->interact_style = SmInteractStyleNone;
	XtInteractPermission(w->session.connection, (SmPointer) w);
    }

    if (w->session.checkpoint_state != XtSaveInactive) {
	if (w->session.save->save_tokens == 0 &&
	    w->session.checkpoint_state == XtSaveActive) {
	    w->session.checkpoint_state = XtSaveInactive;
	    SmcSaveYourselfDone(w->session.connection,
				w->session.save->save_success);
	    CleanUpSave(w);
	}
    }
}

static XtCheckpointToken GetToken(
    Widget	widget,
    int		type)
{
    SessionShellWidget w = (SessionShellWidget) widget;
    XtCheckpointToken token;
    XtSaveYourself save = w->session.save;

    if (type == XtSessionCheckpoint)
	w->session.save->save_tokens++;
    else if (type == XtSessionInteract)
	w->session.save->interact_tokens++;
    else
	return (XtCheckpointToken) NULL;

    token = (XtCheckpointToken) __XtMalloc(sizeof(XtCheckpointTokenRec));
    token->save_type = save->save_type;
    token->interact_style = save->interact_style;
    token->shutdown = save->shutdown;
    token->fast = save->fast;
    token->cancel_shutdown = save->cancel_shutdown;
    token->phase = save->phase;
    token->interact_dialog_type = save->interact_dialog_type;
    token->request_cancel = save->request_cancel;
    token->request_next_phase = save->request_next_phase;
    token->save_success = save->save_success;
    token->type = type;
    token->widget = widget;
    return token;
}

XtCheckpointToken XtSessionGetToken(Widget widget)
{
    SessionShellWidget w = (SessionShellWidget) widget;
    XtCheckpointToken token = NULL;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    if (w->session.checkpoint_state)
	token = GetToken(widget, XtSessionCheckpoint);

    UNLOCK_APP(app);
    return token;
}

static Boolean ExamineToken(
    XtPointer	call_data)
{
    XtCheckpointToken token = (XtCheckpointToken) call_data;
    SessionShellWidget w = (SessionShellWidget) token->widget;

    if (token->interact_dialog_type == SmDialogError)
	w->session.save->interact_dialog_type = SmDialogError;
    if (token->request_next_phase)
	w->session.save->request_next_phase = True;
    if (! token->save_success)
	w->session.save->save_success = False;

    token->interact_dialog_type = w->session.save->interact_dialog_type;
    token->request_next_phase = w->session.save->request_next_phase;
    token->save_success = w->session.save->save_success;
    token->cancel_shutdown = w->session.save->cancel_shutdown;

    return True;
}

void XtSessionReturnToken(XtCheckpointToken token)
{
    SessionShellWidget w = (SessionShellWidget) token->widget;
    Boolean has_some;
    Boolean phase_done;
    XtCallbackProc callback;
    XtPointer client_data;
    WIDGET_TO_APPCON((Widget)w);

    LOCK_APP(app);

    has_some = (XtHasCallbacks(token->widget, XtNinteractCallback)
		== XtCallbackHasSome);

    (void) ExamineToken((XtPointer) token);

    if (token->type == XtSessionCheckpoint) {
	w->session.save->save_tokens--;
	if (has_some && w->session.checkpoint_state == XtSaveActive) {
	    w->session.checkpoint_state = XtInteractPending;
	    SmcInteractRequest(w->session.connection,
			       w->session.save->interact_dialog_type,
			       XtInteractPermission, (SmPointer) w);
	}
	XtFree((char*) token);
    } else {
	if (token->request_cancel)
	    w->session.save->request_cancel = True;
	token->request_cancel = w->session.save->request_cancel;
	if (has_some) {
	    _XtPeekCallback((Widget)w, w->session.interact_callbacks,
			    &callback, &client_data);
	    XtRemoveCallback((Widget)w, XtNinteractCallback,
			     callback, client_data);
	    (*callback)((Widget)w, client_data, (XtPointer)token);
	} else {
	    w->session.save->interact_tokens--;
	    if (w->session.save->interact_tokens == 0) {
		w->session.checkpoint_state = XtSaveActive;
		if (! w->session.save->cancel_shutdown)
		    SmcInteractDone(w->session.connection,
				    w->session.save->request_cancel);
	    }
	    XtFree((char *) token);
	}
    }

    phase_done = (w->session.save->save_tokens == 0 &&
		  w->session.checkpoint_state == XtSaveActive);

    if (phase_done) {
	if (w->session.save->request_next_phase &&
	    w->session.save->phase == 1) {
	    SmcRequestSaveYourselfPhase2(w->session.connection,
					 XtCallNextPhaseCallbacks,
					 (SmPointer)w);
	} else {
	    w->session.checkpoint_state = XtSaveInactive;
	    SmcSaveYourselfDone(w->session.connection,
				w->session.save->save_success);
	    CleanUpSave(w);
	}
    }

    UNLOCK_APP(app);
}

static Boolean IsInArray(
    String str,
    String *sarray)
{
    if (str == NULL || sarray == NULL)
	return False;
    for (; *sarray; sarray++) {
	if (strcmp(*sarray, str) == 0)
	    return True;
    }
    return False;
}

static String* EditCommand(
    String str,		/* if not NULL, the sm_client_id */
    String *src1,	/* first choice */
    String *src2)	/* alternate */
{
    Boolean have;
    Boolean want;
    int count;
    String *sarray;
    String *s;
    String *new;

    want = (str != NULL);
    sarray = (src1 ? src1 : src2);
    if (! sarray) return NULL;
    have = IsInArray("-xtsessionID", sarray);
    if ((want && have) || (!want && !have)) {
	if (sarray == src1)
	    return src1;
	else
	    return NewStringArray(sarray);
    }

    count = 0;
    for (s = sarray; *s; s++)
	count++;

    if (want) {
	s = new = (String *) __XtMalloc((Cardinal)(count+3) * sizeof(String*));
	*s = *sarray;		s++; sarray++;
	*s = "-xtsessionID";	s++;
	*s = str;		s++;
	for (; --count > 0; s++, sarray++)
	    *s = *sarray;
	*s = (String) NULL;
    } else {
	if (count < 3)
	    return NewStringArray(sarray);
	s = new = (String *) __XtMalloc((Cardinal)(count-1) * sizeof(String*));
	for (; --count >= 0; sarray++) {
	    if (strcmp(*sarray, "-xtsessionID") == 0) {
		sarray++;
		count--;
	    } else {
		*s = *sarray;
		s++;
	    }
	}
	*s = (String) NULL;
    }
    s = new;
    new = NewStringArray(new);
    XtFree((char *)s);
    return new;
}

#endif /* !XT_NO_SM */
