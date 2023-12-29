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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xos.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Cardinals.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/DialogP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

/*
 * After we have set the string in the value widget we set the
 * string to a magic value.  So that when a SetValues request is made
 * on the dialog value we will notice it, and reset the string
 */
#define MAGIC_VALUE	((char *)3)

#define streq(a,b)	(strcmp((a), (b)) == 0)

/*
 * Class Methods
 */
static void XawDialogConstraintInitialize(Widget, Widget,
					  ArgList, Cardinal*);
static void XawDialogGetValuesHook(Widget, ArgList, Cardinal*);
static void XawDialogInitialize(Widget, Widget, ArgList, Cardinal*);
static Boolean XawDialogSetValues(Widget, Widget, Widget,
				  ArgList, Cardinal*);

/*
 * Prototypes
 */
static void CreateDialogValueWidget(Widget);

/*
 * Initialization
 */
static XtResource resources[] = {
  {
    XtNlabel,
    XtCLabel,
    XtRString,
    sizeof(String),
    XtOffsetOf(DialogRec, dialog.label),
    XtRString,
    NULL
  },
  {
    XtNvalue,
    XtCValue,
    XtRString,
    sizeof(String),
    XtOffsetOf(DialogRec, dialog.value),
    XtRString,
    NULL
  },
  {
    XtNicon,
    XtCIcon,
    XtRBitmap,
    sizeof(Pixmap),
    XtOffsetOf(DialogRec, dialog.icon),
    XtRImmediate,
    NULL
  },
};

DialogClassRec dialogClassRec = {
  /* core */
  {
    (WidgetClass)&formClassRec,		/* superclass */
    "Dialog",				/* class_name */
    sizeof(DialogRec),			/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part init */
    False,				/* class_inited */
    XawDialogInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    XtInheritRealize,			/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    NULL,				/* destroy */
    XtInheritResize,			/* resize */
    XtInheritExpose,			/* expose */
    XawDialogSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    XawDialogGetValuesHook,		/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XtInheritQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* composite */
  {
    XtInheritGeometryManager,		/* geometry_manager */
    XtInheritChangeManaged,		/* change_managed */
    XtInheritInsertChild,		/* insert_child */
    XtInheritDeleteChild,		/* delete_child */
    NULL,				/* extension */
  },
  /* constraint */
  {
    NULL,				/* subresourses */
    0,					/* subresource_count */
    sizeof(DialogConstraintsRec),	/* constraint_size */
    XawDialogConstraintInitialize,	/* initialize */
    NULL,				/* destroy */
    NULL,				/* set_values */
    NULL,				/* extension */
  },
  /* form */
  {
    XtInheritLayout,			/* layout */
  },
  /* dialog */
  {
    NULL,				/* extension */
  }
};

WidgetClass dialogWidgetClass = (WidgetClass)&dialogClassRec;

/*
 * Implementation
 */
/*ARGSUSED*/
static void
XawDialogInitialize(Widget request _X_UNUSED, Widget cnew,
		    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    DialogWidget dw = (DialogWidget)cnew;
    Arg arglist[9];
    Cardinal arg_cnt = 0;

    XtSetArg(arglist[arg_cnt], XtNborderWidth, 0);		    arg_cnt++;
    XtSetArg(arglist[arg_cnt], XtNleft, XtChainLeft);		    arg_cnt++;

    if (dw->dialog.icon != (Pixmap)0) {
	XtSetArg(arglist[arg_cnt], XtNbitmap, dw->dialog.icon);     arg_cnt++;
	XtSetArg(arglist[arg_cnt], XtNright, XtChainLeft);	    arg_cnt++;
	dw->dialog.iconW = XtCreateManagedWidget("icon", labelWidgetClass,
						 cnew, arglist, arg_cnt);
	arg_cnt = 2;
	XtSetArg(arglist[arg_cnt], XtNfromHoriz, dw->dialog.iconW); arg_cnt++;
    }
    else
	dw->dialog.iconW = NULL;

    XtSetArg(arglist[arg_cnt], XtNlabel, dw->dialog.label);	    arg_cnt++;
    XtSetArg(arglist[arg_cnt], XtNright, XtChainRight);		    arg_cnt++;

    dw->dialog.labelW = XtCreateManagedWidget("label", labelWidgetClass,
					      cnew, arglist, arg_cnt);

    if (dw->dialog.iconW != NULL &&
        XtHeight(dw->dialog.labelW) < XtHeight(dw->dialog.iconW)) {
	XtSetArg(arglist[0], XtNheight, XtHeight(dw->dialog.iconW));
	XtSetValues(dw->dialog.labelW, arglist, 1);
    }
    if (dw->dialog.value != NULL)
	CreateDialogValueWidget((Widget)dw);
    else
        dw->dialog.valueW = NULL;
}

/*ARGSUSED*/
static void
XawDialogConstraintInitialize(Widget request _X_UNUSED, Widget cnew,
			      ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    DialogWidget dw = (DialogWidget)cnew->core.parent;
    DialogConstraints constraint = (DialogConstraints)cnew->core.constraints;

    if (!XtIsSubclass(cnew, commandWidgetClass)) /* if not a button */
	return;					 /* then just use defaults */

    constraint->form.left = constraint->form.right = XtChainLeft;
    if (dw->dialog.valueW == NULL)
	constraint->form.vert_base = dw->dialog.labelW;
    else
	constraint->form.vert_base = dw->dialog.valueW;

    if (dw->composite.num_children > 1) {
	WidgetList children = dw->composite.children;
	Widget *childP;

        for (childP = children + dw->composite.num_children - 1;
	   childP >= children; childP-- ) {
	    if (*childP == dw->dialog.labelW || *childP == dw->dialog.valueW)
		break;
	    if (XtIsManaged(*childP) &&
	        XtIsSubclass(*childP, commandWidgetClass)) {
		constraint->form.horiz_base = *childP;
		break;
	    }
	}
    }
}

#define ICON 0
#define LABEL 1
#define NUM_CHECKS 2
/*ARGSUSED*/
static Boolean
XawDialogSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		   ArgList in_args, Cardinal *in_num_args)
{
    DialogWidget w = (DialogWidget)cnew;
    DialogWidget old = (DialogWidget)current;
    Arg args[5];
    Cardinal num_args;
    unsigned int i;
    Bool checks[NUM_CHECKS];

    for (i = 0; i < NUM_CHECKS; i++)
	checks[i] = False;

    for (i = 0; i < *in_num_args; i++) {
	if (streq(XtNicon, in_args[i].name))
	    checks[ICON] = True;
	else if (streq(XtNlabel, in_args[i].name))
	    checks[LABEL] = True;
    }

    if (checks[ICON]) {
	if (w->dialog.icon != 0) {
	    XtSetArg(args[0], XtNbitmap, w->dialog.icon);
	    if (old->dialog.iconW != NULL)
		XtSetValues(old->dialog.iconW, args, 1);
	    else {
		XtSetArg(args[1], XtNborderWidth, 0);
		XtSetArg(args[2], XtNleft, XtChainLeft);
		XtSetArg(args[3], XtNright, XtChainLeft);
		w->dialog.iconW = XtCreateWidget("icon", labelWidgetClass,
						 cnew, args, 4);
		((DialogConstraints)w->dialog.labelW->core.constraints)->
		    form.horiz_base = w->dialog.iconW;
		XtManageChild(w->dialog.iconW);
	    }
	}
	else if (old->dialog.icon != 0) {
	    ((DialogConstraints)w->dialog.labelW->core.constraints)->
		form.horiz_base = NULL;
	    XtDestroyWidget(old->dialog.iconW);
	    w->dialog.iconW = NULL;
	}
    }

    if (checks[LABEL]) {
	num_args = 0;
	XtSetArg(args[num_args], XtNlabel, w->dialog.label);	num_args++;
	if (w->dialog.iconW != NULL &&
	    XtHeight(w->dialog.labelW) <= XtHeight(w->dialog.iconW)) {
	    XtSetArg(args[num_args], XtNheight, XtHeight(w->dialog.iconW));
	    num_args++;
	}
	XtSetValues(w->dialog.labelW, args, num_args);
    }

    if (w->dialog.value != old->dialog.value) {
	if (w->dialog.value == NULL)	/* only get here if it
					   wasn't NULL before */
	    XtDestroyWidget(old->dialog.valueW);
	else if (old->dialog.value == NULL) {	/* create a new value widget */
	    XtWidth(w) = XtWidth(old);
	    XtHeight(w) = XtHeight(old);
	    CreateDialogValueWidget(cnew);
	}
	else {				/* Widget ok, just change string */
	    Arg nargs[1];

	    XtSetArg(nargs[0], XtNstring, w->dialog.value);
	    XtSetValues(w->dialog.valueW, nargs, 1);
	    w->dialog.value = MAGIC_VALUE;
	}
    }

    return (False);
}

/*
 * Function:
 *	XawDialogGetValuesHook
 *
 * Parameters:
 *	w	 - Dialog Widget
 *	args	 - argument list
 *	num_args - number of args
 *
 * Description:
 *	This is a get values hook routine that gets the values in the dialog.
 */
static void
XawDialogGetValuesHook(Widget w, ArgList args, Cardinal *num_args)
{
    Arg a[1];
    char * s;
    DialogWidget src = (DialogWidget)w;
    unsigned int i;

    for (i = 0; i < *num_args; i++)
	if (streq(args[i].name, XtNvalue)) {
	    XtSetArg(a[0], XtNstring, &s);
	    XtGetValues(src->dialog.valueW, a, 1);
	    *((char **)args[i].value) = s;
	}
	else if (streq(args[i].name, XtNlabel)) {
	    XtSetArg(a[0], XtNlabel, &s);
	    XtGetValues(src->dialog.labelW, a, 1);
	    *((char **)args[i].value) = s;
	}
}

/*
 * Function:
 *	CreateDialogValueWidget
 *
 * Parameters:
 *	w - dialog widget
 *
 * Description:
 *	Creates the dialog widgets value widget.
 *
 * Note
 *	Must be called only when w->dialog.value is non-nil
 */
static void
CreateDialogValueWidget(Widget w)
{
    DialogWidget dw = (DialogWidget)w;
    Arg arglist[10];
    Cardinal num_args = 0;

    XtSetArg(arglist[num_args], XtNstring, dw->dialog.value);     num_args++;
    XtSetArg(arglist[num_args], XtNresizable, True);              num_args++;
    XtSetArg(arglist[num_args], XtNeditType, XawtextEdit);        num_args++;
    XtSetArg(arglist[num_args], XtNfromVert, dw->dialog.labelW);  num_args++;
    XtSetArg(arglist[num_args], XtNleft, XtChainLeft);            num_args++;
    XtSetArg(arglist[num_args], XtNright, XtChainRight);          num_args++;

    dw->dialog.valueW = XtCreateWidget("value", asciiTextWidgetClass,
				       w, arglist, num_args);

    /* if the value widget is being added after buttons,
     * then the buttons need new layout constraints
     */
    if (dw->composite.num_children > 1) {
	WidgetList children = dw->composite.children;
	Widget *childP;

        for (childP = children + dw->composite.num_children - 1;
	     childP >= children; childP-- ) {
	    if (*childP == dw->dialog.labelW || *childP == dw->dialog.valueW)
		continue;

	    if (XtIsManaged(*childP) &&
	        XtIsSubclass(*childP, commandWidgetClass)) {
		((DialogConstraints)(*childP)->core.constraints)->
		    form.vert_base = dw->dialog.valueW;
	    }
	}
    }
    XtManageChild(dw->dialog.valueW);

    /*
     * Value widget gets the keyboard focus
     */
    XtSetKeyboardFocus(w, dw->dialog.valueW);
    dw->dialog.value = MAGIC_VALUE;
}

void
XawDialogAddButton(Widget dialog, _Xconst char* name, XtCallbackProc function,
		   XtPointer param)
{
    /*
     * Correct Constraints are all set in ConstraintInitialize()
     */
    Widget button;

    button = XtCreateManagedWidget(name, commandWidgetClass, dialog, NULL, 0);

    if (function != NULL)	/* don't add NULL callback func */
	XtAddCallback(button, XtNcallback, function, param);
}

char *
XawDialogGetValueString(Widget w)
{
    Arg args[1];
    char *value;

    XtSetArg(args[0], XtNstring, &value);
    XtGetValues(((DialogWidget)w)->dialog.valueW, args, 1);

    return(value);
}
