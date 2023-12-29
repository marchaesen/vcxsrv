/*
Copyright 1989, 1994, 1998  The Open Group

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

/*
 * SimpleMenu.c - Source code file for SimpleMenu widget.
 *
 * Date:    April 3, 1989
 *
 * By:      Chris D. Peterson
 *          MIT X Consortium
 *          kit@expo.lcs.mit.edu
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/Initer.h>
#include <X11/Xaw/Cardinals.h>
#include <X11/Xaw/SimpleMenP.h>
#include <X11/Xaw/SmeBSBP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#define streq(a, b)	(strcmp((a), (b)) == 0)

#define ForAllChildren(smw, childP)				\
for ((childP) = (SmeObject *)(smw)->composite.children;		\
     (childP) < (SmeObject *)((smw)->composite.children		\
			      + (smw)->composite.num_children);	\
     (childP)++)

#ifndef OLDXAW
#define	SMW_UNMAPPING	0x01
#define SMW_POPLEFT	0x02
#endif

/*
 * Class Methods
 */
static void XawSimpleMenuChangeManaged(Widget);
static void XawSimpleMenuClassInitialize(void);
static void XawSimpleMenuClassPartInitialize(WidgetClass);
static XtGeometryResult XawSimpleMenuGeometryManager(Widget, XtWidgetGeometry*,
						     XtWidgetGeometry*);
static void XawSimpleMenuInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawSimpleMenuRealize(Widget, XtValueMask*, XSetWindowAttributes*);
static void XawSimpleMenuRedisplay(Widget, XEvent*, Region);
static void XawSimpleMenuResize(Widget);
static Boolean XawSimpleMenuSetValues(Widget, Widget, Widget,
				      ArgList, Cardinal*);
static Boolean XawSimpleMenuSetValuesHook(Widget, ArgList, Cardinal*);
#ifndef OLDXAW
static void PopupSubMenu(SimpleMenuWidget);
static void PopdownSubMenu(SimpleMenuWidget);
static void PopupCB(Widget, XtPointer, XtPointer);
#endif

/*
 * Prototypes
 */
static void AddPositionAction(XtAppContext, XPointer);
static void CalculateNewSize(Widget, Dimension*, Dimension*);
static void ChangeCursorOnGrab(Widget, XtPointer, XtPointer);
static void CreateLabel(Widget);
static SmeObject DoGetEventEntry(Widget, int, int);
static Widget FindMenu(Widget, String);
static SmeObject GetEventEntry(Widget, XEvent*);
static void Layout(Widget, Dimension*, Dimension*);
static void MakeResizeRequest(Widget);
static void MakeSetValuesRequest(Widget, unsigned int, unsigned int);
static void MoveMenu(Widget, int, int);
static void PositionMenu(Widget, XPoint*);

/*
 * Actions
 */
static void Highlight(Widget, XEvent*, String*, Cardinal*);
static void Notify(Widget, XEvent*, String*, Cardinal*);
#ifndef OLDXAW
static void Popdown(Widget, XEvent*, String*, Cardinal*);
#endif
static void PositionMenuAction(Widget, XEvent*, String*, Cardinal*);
static void Unhighlight(Widget, XEvent*, String*, Cardinal*);

/*
 * Initialization
 */
#define offset(field)	XtOffsetOf(SimpleMenuRec, simple_menu.field)

static XtResource resources[] = {
  /* label */
  {
    XtNlabel,
    XtCLabel,
    XtRString,
    sizeof(String),
    offset(label_string),
    XtRString,
    NULL
  },
  {
    XtNlabelClass,
    XtCLabelClass,
    XtRPointer,
    sizeof(WidgetClass),
    offset(label_class),
    XtRImmediate,
    NULL
  },

  /* layout */
  {
    XtNrowHeight,
    XtCRowHeight,
    XtRDimension,
    sizeof(Dimension),
    offset(row_height),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNtopMargin,
    XtCVerticalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(top_margin),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNbottomMargin,
    XtCVerticalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(bottom_margin),
    XtRImmediate,
    (XtPointer)0
  },
#ifndef OLDXAW
  {
    XtNleftMargin,
    XtCHorizontalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(left_margin),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNrightMargin,
    XtCHorizontalMargins,
    XtRDimension,
    sizeof(Dimension),
    offset(right_margin),
    XtRImmediate,
    (XtPointer)0
  },
#endif

  /* misc */
  {
    XtNallowShellResize,
    XtCAllowShellResize,
    XtRBoolean,
    sizeof(Boolean),
    XtOffsetOf(SimpleMenuRec, shell.allow_shell_resize),
    XtRImmediate,
    (XtPointer)True
  },
  {
    XtNcursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(cursor),
    XtRImmediate,
    (XtPointer)None
  },
  {
    XtNmenuOnScreen,
    XtCMenuOnScreen,
    XtRBoolean,
    sizeof(Boolean),
    offset(menu_on_screen),
    XtRImmediate,
    (XtPointer)True
  },
  {
    XtNpopupOnEntry,
    XtCPopupOnEntry,
    XtRWidget,
    sizeof(Widget),
    offset(popup_entry),
    XtRWidget,
    NULL
  },
  {
    XtNbackingStore,
    XtCBackingStore,
    XtRBackingStore,
    sizeof(int),
    offset(backing_store),
    XtRImmediate,
    (XtPointer)(Always + WhenMapped + NotUseful)
  },
#ifndef OLDXAW
  {
    XawNdisplayList,
    XawCDisplayList,
    XawRDisplayList,
    sizeof(XawDisplayList*),
    offset(display_list),
    XtRImmediate,
    NULL
  },
#endif
};
#undef offset

static char defaultTranslations[] =
"<Enter>:"	"highlight()\n"
"<Leave>:"	"unhighlight()\n"
"<BtnMotion>:"	"highlight()\n"
#ifndef OLDXAW
"<BtnUp>:"	"popdown() notify() unhighlight()\n"
#else
"<BtnUp>:"	"MenuPopdown() notify() unhighlight()\n"
#endif
;

static XtActionsRec actionsList[] =
{
  {"notify",            Notify},
  {"highlight",         Highlight},
  {"unhighlight",       Unhighlight},
#ifndef OLDXAW
  {"popdown",		Popdown},
  {"set-values",	XawSetValuesAction},
  {"get-values",	XawGetValuesAction},
  {"declare",		XawDeclareAction},
  {"call-proc",		XawCallProcAction},
#endif
};

static CompositeClassExtensionRec extension_rec = {
  NULL,					/* next_extension */
  NULLQUARK,				/* record_type */
  XtCompositeExtensionVersion,		/* version */
  sizeof(CompositeClassExtensionRec),	/* record_size */
  True,					/* accepts_objects */
};

#define Superclass	(&overrideShellClassRec)
SimpleMenuClassRec simpleMenuClassRec = {
  /* core */
  {
    (WidgetClass)Superclass,		/* superclass */
    "SimpleMenu",			/* class_name */
    sizeof(SimpleMenuRec),		/* size */
    XawSimpleMenuClassInitialize,	/* class_initialize */
    XawSimpleMenuClassPartInitialize,	/* class_part_initialize */
    False,				/* class_inited */
    XawSimpleMenuInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    XawSimpleMenuRealize,		/* realize */
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
    XawSimpleMenuResize,		/* resize */
    XawSimpleMenuRedisplay,		/* expose */
    XawSimpleMenuSetValues,		/* set_values */
    XawSimpleMenuSetValuesHook,		/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* intrinsics version */
    NULL,				/* callback offsets */
    defaultTranslations,		/* tm_table */
    NULL,				/* query_geometry */
    NULL,				/* display_accelerator */
    NULL,				/* extension */
  },
  /* composite */
  {
    XawSimpleMenuGeometryManager,	/* geometry_manager */
    XawSimpleMenuChangeManaged,		/* change_managed */
    XtInheritInsertChild,		/* insert_child */
    XtInheritDeleteChild,		/* delete_child */
    NULL,				/* extension */
  },
  /* shell */
  {
    NULL,				/* extension */
  },
  /* override */
  {
    NULL,				/* extension */
  },
  /* simple_menu */
  {
    NULL,				/* extension */
  },
};

WidgetClass simpleMenuWidgetClass = (WidgetClass)&simpleMenuClassRec;

/*
 * Implementation
 */
/*
 * Function:
 *	XawSimpleMenuClassInitialize
 *
 * Description:
 *	Class Initialize routine, called only once.
 */
static void
XawSimpleMenuClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtAddConverter(XtRString, XtRBackingStore, XmuCvtStringToBackingStore,
		   NULL, 0);
    XtSetTypeConverter(XtRBackingStore, XtRString, XmuCvtBackingStoreToString,
		       NULL, 0, XtCacheNone, NULL);
    XmuAddInitializer(AddPositionAction, NULL);
}

/*
 * Function:
 *	XawSimpleMenuClassPartInitialize
 *      Arguments: wc - the widget class of the subclass.
 *
 * Description:
 *	  Class Part Initialize routine, called for every subclass.  Makes
 *	sure that the subclasses pick up the extension record.
 */
static void
XawSimpleMenuClassPartInitialize(WidgetClass wc)
{
    SimpleMenuWidgetClass smwc = (SimpleMenuWidgetClass)wc;

    /*
     * Make sure that our subclass gets the extension rec too
     */
    extension_rec.next_extension = smwc->composite_class.extension;
    smwc->composite_class.extension = (XtPointer) &extension_rec;
}

/*
 *  Function:
 *	XawSimpleMenuInitialize
 *
 * Parameters:
 *	request - widget requested by the argument list
 *	cnew	- new widget with both resource and non resource values
 *
 * Description:
 *	Initializes the simple menu widget.
 */
/*ARGSUSED*/
static void
XawSimpleMenuInitialize(Widget request _X_UNUSED, Widget cnew,
			ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)cnew;
    Dimension width, height;

    XmuCallInitializers(XtWidgetToApplicationContext(cnew));

    if (smw->simple_menu.label_class == NULL)
	smw->simple_menu.label_class = smeBSBObjectClass;

    smw->simple_menu.label = NULL;
    smw->simple_menu.entry_set = NULL;
    smw->simple_menu.recursive_set_values = False;
#ifndef OLDXAW
    smw->simple_menu.sub_menu = NULL;
    smw->simple_menu.state = 0;

    XtAddCallback(cnew, XtNpopupCallback, PopupCB, NULL);
#endif

    if (smw->simple_menu.label_string != NULL)
	CreateLabel(cnew);

    width = height = 0;
    CalculateNewSize(cnew, &width, &height);

    smw->simple_menu.menu_width = True;

    if (XtWidth(smw) == 0) {
	smw->simple_menu.menu_width = False;
	XtWidth(smw) = width;
    }

    smw->simple_menu.menu_height = True;

    if (XtHeight(smw) == 0) {
	smw->simple_menu.menu_height = False;
	XtHeight(smw) = height;
    }

    /*
     * Add a popup_callback routine for changing the cursor
     */
    XtAddCallback(cnew, XtNpopupCallback, ChangeCursorOnGrab, NULL);
}

/*
 * Function:
 *	XawSimpleMenuRedisplay
 *
 * Parameters:
 *	w      - simple menu widget
 *	event  - X event that caused this redisplay
 *	region - region the needs to be repainted
 *
 * Description:
 *	Redisplays the contents of the widget.
 */
/*ARGSUSED*/
static void
XawSimpleMenuRedisplay(Widget w, XEvent *event _X_UNUSED, Region region)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    SmeObject *entry;
    SmeObjectClass cclass;

    if (region == NULL)
	XClearWindow(XtDisplay(w), XtWindow(w));

#ifndef OLDXAW
    if (smw->simple_menu.display_list)
      XawRunDisplayList(w, smw->simple_menu.display_list, event, region);
#endif

    /*
     * Check and Paint each of the entries - including the label
     */
    ForAllChildren(smw, entry) {
	if (!XtIsManaged((Widget)*entry))
	    continue;

	if (region != NULL)
	    switch(XRectInRegion(region, XtX(*entry),XtY(*entry),
				 XtWidth(*entry), XtHeight(*entry))) {
		case RectangleIn:
		case RectanglePart:
		    break;
		default:
		    continue;
	    }

	cclass = (SmeObjectClass)(*entry)->object.widget_class;

	if (cclass->rect_class.expose != NULL)
	    (cclass->rect_class.expose)((Widget)*entry, NULL, NULL);
    }
}

/*
 * Function:
 *	XawSimpleMenuRealize
 *
 * Parameters:
 *	w     - simple menu widget
 *	mask  - value mask for the window to create
 *	attrs - attributes for the window to create
 *
 * Description:
 *	Realizes the widget.
 */
static void
XawSimpleMenuRealize(Widget w, XtValueMask *mask, XSetWindowAttributes *attrs)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
#ifndef OLDXAW
    XawPixmap *pixmap;
#endif

    attrs->cursor = smw->simple_menu.cursor;
    *mask |= CWCursor;
    if (smw->simple_menu.backing_store == Always ||
	smw->simple_menu.backing_store == NotUseful ||
	smw->simple_menu.backing_store == WhenMapped) {
	*mask |= CWBackingStore;
	attrs->backing_store = smw->simple_menu.backing_store;
    }
    else
	*mask &= (XtValueMask)(~CWBackingStore);

    (*Superclass->core_class.realize)(w, mask, attrs);

#ifndef OLDXAW
    if (w->core.background_pixmap > XtUnspecifiedPixmap) {
	pixmap = XawPixmapFromXPixmap(w->core.background_pixmap, XtScreen(w),
				      w->core.colormap, (int)w->core.depth);
	if (pixmap && pixmap->mask)
	    XawReshapeWidget(w, pixmap);
    }
#endif
}

/*
 * Function:
 *	XawSimpleMenuResize
 *
 * Parameters:
 *	w - simple menu widget
 *
 * Description:
 *	Handle the menu being resized.
 */
static void
XawSimpleMenuResize(Widget w)
{
    if (!XtIsRealized(w))
	return;

    Layout(w, NULL, NULL);

    XawSimpleMenuRedisplay(w, NULL, NULL);
}

/*
 * Function:
 *	XawSimpleMenuSetValues
 *
 * Parameters:
 *	current - current state of the widget
 *	request - what was requested
 *	cnew    - what the widget will become
 *
 * Description:
 *	Relayout the menu when one of the resources is changed.
 */
/*ARGSUSED*/
static Boolean
XawSimpleMenuSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		       ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    SimpleMenuWidget smw_old = (SimpleMenuWidget)current;
    SimpleMenuWidget smw_new = (SimpleMenuWidget)cnew;
    Boolean ret_val = False, layout = False;

    if (!XtIsRealized(current))
	return (False);

    if (!smw_new->simple_menu.recursive_set_values) {
	if (XtWidth(smw_new) != XtWidth(smw_old)) {
	    smw_new->simple_menu.menu_width = XtWidth(smw_new) != 0;
	    layout = True;
	}
	if (XtHeight(smw_new) != XtHeight(smw_old)) {
	    smw_new->simple_menu.menu_height = XtHeight(smw_new) != 0;
	    layout = True;
	}
    }

    if (smw_old->simple_menu.cursor != smw_new->simple_menu.cursor)
	XDefineCursor(XtDisplay(cnew), XtWindow(cnew),
		      smw_new->simple_menu.cursor);

    if (smw_old->simple_menu.label_string !=smw_new->simple_menu.label_string) {
	if (smw_new->simple_menu.label_string == NULL)	    /* Destroy */
	    XtDestroyWidget((Widget)smw_old->simple_menu.label);
	else if (smw_old->simple_menu.label_string == NULL) /* Create */
	    CreateLabel(cnew);
	else {						    /* Change */
	    Arg arglist[1];

	    XtSetArg(arglist[0], XtNlabel, smw_new->simple_menu.label_string);
	    XtSetValues((Widget)smw_new->simple_menu.label, arglist, ONE);
	}
    }

    if (smw_old->simple_menu.label_class != smw_new->simple_menu.label_class)
	XtAppWarning(XtWidgetToApplicationContext(cnew),
		     "No Dynamic class change of the SimpleMenu Label.");

    if (smw_old->simple_menu.top_margin != smw_new->simple_menu.top_margin
	|| smw_old->simple_menu.bottom_margin
	!= smw_new->simple_menu.bottom_margin) {
	layout = True;
	ret_val = True;
    }

#ifndef OLDXAW
    if (smw_old->core.background_pixmap != smw_new->core.background_pixmap) {
	XawPixmap *opix, *npix;

	opix = XawPixmapFromXPixmap(smw_old->core.background_pixmap,
				    XtScreen(smw_old), smw_old->core.colormap,
				    (int)smw_old->core.depth);
	npix = XawPixmapFromXPixmap(smw_new->core.background_pixmap,
				    XtScreen(smw_new), smw_new->core.colormap,
				    (int)smw_new->core.depth);
	if ((npix && npix->mask) || (opix && opix->mask))
	    XawReshapeWidget(cnew, npix);
    }
#endif

    if (layout)
	Layout(cnew, NULL, NULL);

    return (ret_val);
}

/*
 * Function:
 *	XawSimpleMenuSetValuesHook
 *
 * Parameters:
 *	w	 - menu widget
 *	arglist	 - argument list passed to XtSetValues
 *	num_args - number of args
 *
 * Description:
 *	To handle a special case, this is passed the actual arguments.
 */
static Boolean
XawSimpleMenuSetValuesHook(Widget w, ArgList arglist, Cardinal *num_args)
{
    Cardinal i;
    Dimension width, height;

    width = XtWidth(w);
    height = XtHeight(w);

    for (i = 0 ; i < *num_args ; i++) {
	if (streq(arglist[i].name, XtNwidth))
	    width = (Dimension)arglist[i].value;
	if (streq(arglist[i].name, XtNheight))
	    height = (Dimension) arglist[i].value;
    }

    if (width != XtWidth(w) || height != XtHeight(w))
	MakeSetValuesRequest(w, width, height);

    return (False);
}

/*
 * Geometry Management routines
 */
/*
 * Function:
 *	XawSimpleMenuGeometryManager
 *
 * Parameters:
 *	w	- Menu Entry making the request
 *	request - requested new geometry
 *                 reply - the allowed geometry.
 *
 * Description:
 *	This is the SimpleMenu Widget's Geometry Manager.
 *
 * Returns:
 *	XtGeometry{Yes, No, Almost}
 */
static XtGeometryResult
XawSimpleMenuGeometryManager(Widget w, XtWidgetGeometry *request,
			     XtWidgetGeometry *reply)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)XtParent(w);
    SmeObject entry = (SmeObject)w;
    XtGeometryMask mode = request->request_mode;
    XtGeometryResult answer;
    Dimension old_height, old_width;

    if (!(mode & CWWidth) && !(mode & CWHeight))
	return (XtGeometryNo);

    reply->width = request->width;
    reply->height = request->height;

    old_width = XtWidth(entry);
    old_height = XtHeight(entry);

    Layout(w, &reply->width, &reply->height);

    /*
     * Since we are an override shell and have no parent there is no one to
     * ask to see if this geom change is okay, so I am just going to assume
     * we can do whatever we want.  If you subclass be very careful with this
     * assumption, it could bite you.
     *
     * Chris D. Peterson - Sept. 1989.
     */
    if ((!(mode & CWWidth) || reply->width == request->width)
	&& (!(mode & CWHeight) || reply->height == request->height)) {
	if (mode & XtCWQueryOnly) {	/* Actually perform the layout */
	    XtWidth(entry) = old_width;
	    XtHeight(entry) = old_height;
	}
	else
	    Layout((Widget)smw, NULL, NULL);
	answer = XtGeometryDone;
    }
    else {
	XtWidth(entry) = old_width;
	XtHeight(entry) = old_height;

	if ((reply->width == request->width && !(mode & CWHeight))
	    || (reply->height == request->height && !(mode & CWWidth))
	    || (reply->width == request->width
	    && reply->height == request->height))
	    answer = XtGeometryNo;
	else {
	    answer = XtGeometryAlmost;
	    reply->request_mode = 0;
	    if (reply->width != request->width)
		reply->request_mode |= CWWidth;
	    if (reply->height != request->height)
		reply->request_mode |= CWHeight;
	}
    }

    return (answer);
}

/*
 * Function:
 *	XawSimpleMenuChangeManaged
 *
 * Parameters:
 *	w - simple menu widget
 *
 * Description:
 *	Called whenever a new child is managed.
 */
static void
XawSimpleMenuChangeManaged(Widget w)
{
    Layout(w, NULL, NULL);
}

/*
 * Global Action Routines
 *
 * These actions routines will be added to the application's
 * global action list
 */
/*
 * Function:
 *	PositionMenuAction
 *
 * Parameters:
 *	w	   - a widget (no the simple menu widget)
 *	event	   - the event that caused this action
 *	params	   - parameters passed to the routine.
 *                                      we expect the name of the menu here.
 *	num_params - ""
 *
 * Description:
 *	Positions the simple menu widget.
 */
/*ARGSUSED*/
static void
PositionMenuAction(Widget w, XEvent *event,
		   String *params, Cardinal *num_params)
{
    Widget menu;
    XPoint loc;

    if (*num_params != 1) {
	XtAppWarning(XtWidgetToApplicationContext(w),
		     "SimpleMenuWidget: position menu action expects "
		     "only one parameter which is the name of the menu.");
	return;
    }

    if ((menu = FindMenu(w, params[0])) == NULL) {
	char error_buf[BUFSIZ];

	snprintf(error_buf, sizeof(error_buf),
		 "SimpleMenuWidget: could not find menu named %s.",
		 params[0]);
	XtAppWarning(XtWidgetToApplicationContext(w), error_buf);
	return;
    }

    switch (event->type) {
	case ButtonPress:
	case ButtonRelease:
	    loc.x = (short)event->xbutton.x_root;
	    loc.y = (short)event->xbutton.y_root;
	    PositionMenu(menu, &loc);
	    break;
	case EnterNotify:
	case LeaveNotify:
	    loc.x = (short)event->xcrossing.x_root;
	    loc.y = (short)event->xcrossing.y_root;
	    PositionMenu(menu, &loc);
	    break;
	case MotionNotify:
	    loc.x = (short)event->xmotion.x_root;
	    loc.y = (short)event->xmotion.y_root;
	    PositionMenu(menu, &loc);
	    break;
	default:
	    PositionMenu(menu, NULL);
	    break;
    }
}

/*
 * Widget Action Routines
 */
/*
 * Function:
 *	Unhighlight
 *
 * Parameters:
 *	w	   - simple menu widget
 *	event	   - event that caused this action
 *	params	   - not used
 *	num_params - ""
 *
 * Description:
 *	Unhighlights current entry.
 */
/*ARGSUSED*/
static void
Unhighlight(Widget w, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    SmeObject entry = smw->simple_menu.entry_set;

    if (entry == NULL)
	return;

#ifndef OLDXAW
    if (!smw->simple_menu.sub_menu)
#endif
    {
	SmeObjectClass cclass;

	smw->simple_menu.entry_set = NULL;
	cclass = (SmeObjectClass)entry->object.widget_class;
	(cclass->sme_class.unhighlight)((Widget)entry);
    }
}

/*
 * Function:
 *	Highlight
 *
 * Parameters:
 *	w	   - simple menu widget
 *	event	   - event that caused this action
 *	params	   - not used
 *	num_params - ""
 *
 * Description:
 *	Highlights current entry.
 */
/*ARGSUSED*/
static void
Highlight(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    SmeObject entry;

    if (!XtIsSensitive(w))
	return;

    entry = GetEventEntry(w, event);

    if (entry == smw->simple_menu.entry_set)
	return;

#ifndef OLDXAW
    if (!smw->simple_menu.sub_menu)
#endif
	Unhighlight(w, event, params, num_params);

    if (entry == NULL)
	return;

    if (!XtIsSensitive((Widget)entry))
	return;

#ifndef OLDXAW
    if (smw->simple_menu.sub_menu)
	PopdownSubMenu(smw);
#endif

    Unhighlight(w, event, params, num_params);

#ifndef OLDXAW
    if (!(smw->simple_menu.state & SMW_UNMAPPING))
#endif
    {
	SmeObjectClass cclass;

	smw->simple_menu.entry_set = entry;
	cclass = (SmeObjectClass)entry->object.widget_class;

	(cclass->sme_class.highlight)((Widget)entry);

#ifndef OLDXAW
	if (XtIsSubclass((Widget)entry, smeBSBObjectClass))
	    PopupSubMenu(smw);
#endif
    }
}

/*
 * Function:
 *	Notify
 *
 * Parameters:
 *	w	   - simple menu widget
 *	event	   - event that caused this action
 *	params	   - not used
 *	num_params - ""
 *
 * Description:
 *	Notify user of current entry.
 */
/*ARGSUSED*/
static void
Notify(Widget w, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    SmeObject entry;
    SmeObjectClass cclass;

    /* may be a propagated event from a sub menu, need to check it */
    if (XtWindow(w) != event->xany.window)
	return;
    entry = GetEventEntry(w, event);
    if (entry == NULL || !XtIsSensitive((Widget)entry))
	return;

    cclass = (SmeObjectClass) entry->object.widget_class;
    (cclass->sme_class.notify)((Widget)entry);
}

/*
 * Public Functions
 */
/*
 * Function:
 *	XawSimpleMenuAddGlobalActions
 *
 * Arguments:
 *	app_con - appcontext
 *
 * Description:
 *	Adds the global actions to the simple menu widget.
 */
void
XawSimpleMenuAddGlobalActions(XtAppContext app_con)
{
    XtInitializeWidgetClass(simpleMenuWidgetClass);
    XmuCallInitializers(app_con);
}

/*
 * Function:
 *	XawSimpleMenuGetActiveEntry
 *
 * Parameters:
 *	w - smw widget
 *
 * Description:
 *	Gets the currently active (set) entry.
 *
 * Returns:
 *	The currently set entry or NULL if none is set
 */
Widget
XawSimpleMenuGetActiveEntry(Widget w)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;

    return ((Widget)smw->simple_menu.entry_set);
}

/*
 * Function:
 *	XawSimpleMenuClearActiveEntry
 *
 * Parameters:
 *	w - smw widget
 *
 * Description:
 *	Unsets the currently active (set) entry.
 */
void
XawSimpleMenuClearActiveEntry(Widget w)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;

    smw->simple_menu.entry_set = NULL;
}

/*
 * Private Functions
 */
/*
 * Function:
 *	CreateLabel
 *
 * Parameters:
 *	w - smw widget
 *
 * Description:
 * Creates the label object and makes sure it is the first child in
 * in the list.
 */
static void
CreateLabel(Widget w)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    Widget *child, *next_child;
    int i;
    Arg args[2];

    if (smw->simple_menu.label_string == NULL ||
	smw->simple_menu.label != NULL) {
	XtAppWarning(XtWidgetToApplicationContext(w),
		     "Xaw Simple Menu Widget: label string is NULL or "
		     "label already exists, no label is being created.");
	return;
    }

    XtSetArg(args[0], XtNlabel, smw->simple_menu.label_string);
    XtSetArg(args[1], XtNjustify, XtJustifyCenter);
    smw->simple_menu.label = (SmeObject)
	XtCreateManagedWidget("menuLabel",
			      smw->simple_menu.label_class, w, args, TWO);

    next_child = NULL;
    for (child = smw->composite.children + smw->composite.num_children,
	 i = (int)smw->composite.num_children; i > 0; i--, child--) {
	if (next_child != NULL)
	    *next_child = *child;
	next_child = child;
    }
    *child = (Widget)smw->simple_menu.label;
}

/*
 * Function:
 *	Layout
 *
 * Arguments:
 *	w	   - See below
 *	width_ret  - returned width
 *	height_ret - returned height
 *
 * Note:
 * if width == NULL || height == NULL then it assumes the you do not care
 * about the return values, and just want a relayout.
 *
 * if this is not the case then it will set width_ret and height_ret
 * to be width and height that the child would get if it were laid out
 * at this time.
 *
 *	"w" can be the simple menu widget or any of its object children.
 */
static void
Layout(Widget w, Dimension *width_ret, Dimension *height_ret)
{
    SmeObject current_entry;
    SimpleMenuWidget smw;
    Dimension width, height;
    Boolean allow_change_size;
    Widget kid;
    Cardinal i, count, n;
    int width_kid, height_kid, tmp_w, tmp_h;
    short vadd, hadd, x_ins, y_ins;
    Dimension *widths;

    height = 0;

    if (XtIsSubclass(w, simpleMenuWidgetClass)) {
	smw = (SimpleMenuWidget)w;
	current_entry = NULL;
    }
    else {
	smw = (SimpleMenuWidget)XtParent(w);
	current_entry = (SmeObject)w;
    }

    allow_change_size = (!XtIsRealized((Widget)smw)
			 || smw->shell.allow_shell_resize);

    for (i = smw->simple_menu.label ? 1 : 0;
	 i < smw->composite.num_children;
	 i++) {
	XtWidgetGeometry preferred;

	kid = smw->composite.children[i];
	if (!XtIsManaged(kid))
	    continue;
	if (smw->simple_menu.row_height != 0)
	    XtHeight(kid) = smw->simple_menu.row_height;
	XtQueryGeometry(kid, NULL, &preferred);
	if (preferred.request_mode & CWWidth)
	    XtWidth(kid) = preferred.width;
    }

    if (smw->simple_menu.label
	&& XtIsManaged((Widget)smw->simple_menu.label)) {
	XtWidgetGeometry preferred;

	kid = (Widget)smw->simple_menu.label;
	XtQueryGeometry(kid, NULL, &preferred);
	if (preferred.request_mode & CWWidth)
	    XtWidth(kid) = preferred.width;
	if (preferred.request_mode & CWHeight)
	    XtHeight(kid) = preferred.height;
    }

    /* reset */
    if (!smw->simple_menu.menu_width)
	XtWidth(smw) = 0;
    if (!smw->simple_menu.menu_height)
	XtHeight(smw) = 0;
    if (!XtWidth(smw) || !XtHeight(smw))
	MakeResizeRequest((Widget)smw);

    widths = (Dimension *)XtMalloc(sizeof(Dimension));
#ifndef OLDXAW
    hadd = (short)smw->simple_menu.left_margin;
#else
    hadd = 0;
#endif
    vadd = (short)smw->simple_menu.top_margin;
    if (smw->simple_menu.label)
	vadd = (short)(vadd + XtHeight(smw->simple_menu.label));

    count = 1;
    width = (Dimension)(tmp_w = tmp_h = (int)(n = 0));
    height = (Dimension)vadd;

    for (i = smw->simple_menu.label ? 1 : 0;
	 i < smw->composite.num_children;
	 i++) {
	kid = smw->composite.children[i];
	if (!XtIsManaged(kid))
	    continue;
	width_kid = XtWidth(kid);
	height_kid = XtHeight(kid);

	if (n && (height + height_kid + smw->simple_menu.bottom_margin
		  > XtHeight(smw))) {
	    ++count;
	    widths = (Dimension *)XtRealloc((char *)widths,
					    (Cardinal)(sizeof(Dimension) * count));
	    widths[count - 1] = (Dimension)width_kid;
	    width = (Dimension)(width + tmp_w);
	    tmp_w = width_kid;
	    height = (Dimension)(height_kid + vadd);
	}
	else
	    height = (Dimension)(height + height_kid);
	if (height > tmp_h)
	    tmp_h = height;
	if (width_kid > tmp_w)
	    widths[count - 1] = (Dimension)(tmp_w = width_kid);
	++n;
    }

    height = (tmp_h + smw->simple_menu.bottom_margin);
    width = (Dimension)(width + tmp_w);

    if (smw->simple_menu.label && width < XtWidth(smw->simple_menu.label)) {
	float inc;

	inc = (float)(XtWidth(smw->simple_menu.label) - width) / (float)count;
	width = XtWidth(smw->simple_menu.label);
	for (n = 0; n < count; n++)
	    widths[n] = (Dimension)(widths[n] + inc);
    }

#ifndef OLDXAW
    width = (Dimension)(width + (hadd + smw->simple_menu.right_margin));
#endif

    x_ins = (short)(n = count = 0);
    tmp_w = widths[0];
    tmp_h = vadd;

    for (i = smw->simple_menu.label ? 1 : 0;
	 i < smw->composite.num_children;
	 i++) {
	kid = smw->composite.children[i];
	if (!XtIsManaged(kid))
	    continue;

	height_kid = XtHeight(kid);

	if (n && (tmp_h + height_kid + smw->simple_menu.bottom_margin
		  > XtHeight(smw))) {
	    x_ins = (short)tmp_w;
	    y_ins = vadd;
	    ++count;
	    tmp_w += widths[count];
	    tmp_h = height_kid + vadd;
	}
	else {
	    y_ins = (short)tmp_h;
	    tmp_h += height_kid;
	}
	++n;

	XtX(kid) = (Position)(x_ins + hadd);
	XtY(kid) = y_ins;
	XtWidth(kid) = widths[count];
    }

    XtFree((char *)widths);

    if (allow_change_size)
	MakeSetValuesRequest((Widget) smw, width, height);

    if (smw->simple_menu.label) {
	XtX(smw->simple_menu.label) = 0;
	XtY(smw->simple_menu.label) = (Position)smw->simple_menu.top_margin;
	XtWidth(smw->simple_menu.label) = (Dimension)(XtWidth(smw)
#ifndef OLDXAW
	    - (smw->simple_menu.left_margin + smw->simple_menu.right_margin)
#endif
	    );
    }
    if (current_entry) {
	if (width_ret)
	    *width_ret = XtWidth(current_entry);
	if (height_ret)
	    *height_ret = XtHeight(current_entry);
    }
}

/*
 * Function:
 *	AddPositionAction
 *
 * Parameters:
 *	app_con - application context
 *	data	- (not used)
 *
 * Description:
 *	  Adds the XawPositionSimpleMenu action to the global
 *                   action list for this appcon.
 */
/*ARGSUSED*/
static void
AddPositionAction(XtAppContext app_con, XPointer data _X_UNUSED)
{
    static XtActionsRec pos_action[] = {
	{"XawPositionSimpleMenu",	PositionMenuAction},
    };

    XtAppAddActions(app_con, pos_action, XtNumber(pos_action));
}

/*
 * Function:
 *	FindMenu
 *
 * Parameters:
 *	widget - reference widget
 *	name   - menu widget's name
 *
 * Description:
 *	Find the menu give a name and reference widget
 *
 * Returns:
 *	The menu widget or NULL.
 */
static Widget
FindMenu(Widget widget, String name)
{
    Widget w, menu;

    for (w = widget; w != NULL; w = XtParent(w))
	if ((menu = XtNameToWidget(w, name)) != NULL)
	    return (menu);

    return (NULL);
}

/*
 * Function:
 *	PositionMenu
 *
 * Parameters:
 *	w	 - simple menu widget
 *	location - pointer the the position or NULL
 *
 * Description:
 *	Places the menu
 */
static void
PositionMenu(Widget w, XPoint *location)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    SmeObject entry;
    XPoint t_point;

    if (location == NULL) {
	Window temp1, temp2;
	int root_x, root_y, tempX, tempY;
	unsigned int tempM;

	location = &t_point;
	if (XQueryPointer(XtDisplay(w), XtWindow(w), &temp1, &temp2,
			  &root_x, &root_y, &tempX, &tempY, &tempM) == False) {
	    XtAppWarning(XtWidgetToApplicationContext(w),
			 "Xaw Simple Menu Widget: "
			 "Could not find location of mouse pointer");
	    return;
	}
	location->x = (short) root_x;
	location->y = (short) root_y;
    }

    /*
     * The width will not be correct unless it is realized
     */
    XtRealizeWidget(w);

    location->x = (short)(location->x - (XtWidth(w) >> 1));

    if (smw->simple_menu.popup_entry == NULL)
	entry = smw->simple_menu.label;
    else
	entry = smw->simple_menu.popup_entry;

    if (entry != NULL)
      location->y = (short)(location->y - (XtY(entry) + (XtHeight(entry) >> 1)));

    MoveMenu(w, location->x, location->y);
}

/*
 * Function:
 *	MoveMenu
 *
 * Parameters:
 *	w - simple menu widget
 *	x - current location of the widget
 *	y - ""
 *
 * Description:
 *	  Actually moves the menu, may force it to
 *	to be fully visible if menu_on_screen is True.
 */
static void
MoveMenu(Widget w, int x, int y)
{
    Arg arglist[2];
    Cardinal num_args = 0;
    SimpleMenuWidget smw = (SimpleMenuWidget)w;

    if (smw->simple_menu.menu_on_screen) {
	int width = XtWidth(w) + (XtBorderWidth(w) << 1);
	int height = XtHeight(w) + (XtBorderWidth(w) << 1);

	if (x >= 0) {
	    int scr_width = WidthOfScreen(XtScreen(w));

	    if (x + width > scr_width)
		x = scr_width - width;
	}
	if (x < 0)
	    x = 0;

	if (y >= 0) {
	    int scr_height = HeightOfScreen(XtScreen(w));

	    if (y + height > scr_height)
		y = scr_height - height;
	}
	if (y < 0)
	    y = 0;
    }

    XtSetArg(arglist[num_args], XtNx, x); num_args++;
    XtSetArg(arglist[num_args], XtNy, y); num_args++;
    XtSetValues(w, arglist, num_args);
}

/*
 * Function:
 *	ChangeCursorOnGrab
 *
 * Parameters:
 *	w     - menu widget
 *	temp1 - not used
 *	temp2 - ""
 *
 * Description:
 *	  Changes the cursor on the active grab to the one
 *                   specified in out resource list.
 */
/*ARGSUSED*/
static void
ChangeCursorOnGrab(Widget w, XtPointer temp1 _X_UNUSED, XtPointer temp2 _X_UNUSED)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;

    /*
     * The event mask here is what is currently in the MIT implementation.
     * There really needs to be a way to get the value of the mask out
     * of the toolkit (CDP 5/26/89).
     */
    XChangeActivePointerGrab(XtDisplay(w), ButtonPressMask | ButtonReleaseMask,
			     smw->simple_menu.cursor,
			     XtLastTimestampProcessed(XtDisplay(w)));
}

/*
 * Function:
 *	MakeSetValuesRequest
 *
 * Parameters:
 *	w      - simple menu widget
 *	width  - size requested
 *	height - ""
 */
static void
MakeSetValuesRequest(Widget w, unsigned int width, unsigned int height)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    Arg arglist[2];
    Cardinal num_args = 0;

    if (!smw->simple_menu.recursive_set_values) {
	if (XtWidth(smw) != width || XtHeight(smw) != height) {
	    smw->simple_menu.recursive_set_values = True;
	    XtSetArg(arglist[num_args], XtNwidth, width);   num_args++;
	    XtSetArg(arglist[num_args], XtNheight, height); num_args++;
	    XtSetValues(w, arglist, num_args);
	}
	else if (XtIsRealized((Widget)smw))
	    XawSimpleMenuRedisplay((Widget)smw, NULL, NULL);
    }
    smw->simple_menu.recursive_set_values = False;
}

static SmeObject
DoGetEventEntry(Widget w, int x_loc, int y_loc)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    SmeObject *entry;

    ForAllChildren(smw, entry) {
	if (!XtIsManaged((Widget)*entry))
	    continue;

	if (x_loc > XtX(*entry)
	    && x_loc <= XtX(*entry) + XtWidth(*entry)
	    && y_loc > XtY(*entry)
	    &&  y_loc <= XtY(*entry) + XtHeight(*entry)) {
	    if (*entry == smw->simple_menu.label)
		return (NULL);	/* cannot select the label */
	    else
		return (*entry);
	}
    }

    return (NULL);
}

/*
 * Function:
 *	GetEventEntry
 *
 * Parameters:
 *	w     - simple menu widget
 *	event - X event
 *
 * Description:
 *	Gets an entry given an event that has X and Y coords.
 *
 * Returns:
 *	The entry that this point is in
 */
static SmeObject
GetEventEntry(Widget w, XEvent *event)
{
    int x_loc, y_loc, x_root;
    SimpleMenuWidget smw = (SimpleMenuWidget)w;
    SmeObject entry;
    int warp, move;

    switch (event->type) {
	case MotionNotify:
	    x_loc = event->xmotion.x;
	    y_loc = event->xmotion.y;
	    x_root = event->xmotion.x_root;
	    break;
	case EnterNotify:
	case LeaveNotify:
	    x_loc = event->xcrossing.x;
	    y_loc = event->xcrossing.y;
	    x_root = event->xcrossing.x_root;
	    break;
	case ButtonPress:
	case ButtonRelease:
	    x_loc = event->xbutton.x;
	    y_loc = event->xbutton.y;
	    x_root = event->xbutton.x_root;
	    break;
	default:
	    XtAppError(XtWidgetToApplicationContext(w),
		       "Unknown event type in GetEventEntry().");
	    return (NULL);
    }

    if (x_loc < 0 || x_loc >= XtWidth(smw) ||
	y_loc < 0 || y_loc >= XtHeight(smw))
	return (NULL);

    /* Move the menu if it's outside the screen, does not check
     * smw->simple_menu.menu_on_screen because menus is bigger than screen
     */
    if (x_root == WidthOfScreen(XtScreen(w)) - 1 &&
	XtX(w) + XtWidth(w) + (XtBorderWidth(w)) > x_root) {
	warp = -8;
	if (smw->simple_menu.entry_set) {
	    entry = DoGetEventEntry(w,
				    XtX(smw->simple_menu.entry_set)
				    + XtWidth(smw->simple_menu.entry_set) + 1,
				    y_loc);
	    Unhighlight(w, event, NULL, NULL);
	    if (entry) {
		warp = -(int)XtWidth(entry) >> 1;
		move = x_loc - XtWidth(entry) - XtX(entry) + XtBorderWidth(w);
	    }
	    else {
		warp = 0;
		move = WidthOfScreen(XtScreen(w)) -
		       (XtX(w) + XtWidth(w) + (XtBorderWidth(w) << 1));
	    }
	}
	else {
	    warp = 0;
	    move = WidthOfScreen(XtScreen(w)) -
		   (XtX(w) + XtWidth(w) + (XtBorderWidth(w) << 1));
	}
    }
    else if (x_root == 0 && XtX(w) < 0) {
	warp = 8;
	if (smw->simple_menu.entry_set) {
	    entry = DoGetEventEntry(w, XtX(smw->simple_menu.entry_set) - 1,
				    y_loc);
	    Unhighlight(w, event, NULL, NULL);
	    if (entry) {
		warp = XtWidth(entry) >> 1;
		move = x_loc - XtX(entry);
	    }
	    else
		move = x_loc + XtBorderWidth(w);
	}
	else
	    move = x_loc + XtBorderWidth(w);
    }
    else
	move = warp = 0;

    if (move)
	XtMoveWidget(w, (Position)(XtX(w) + move), XtY(w));
    if (warp)
	XWarpPointer(XtDisplay(w), None, None, 0, 0, 0, 0, warp, 0);

    return (DoGetEventEntry(w, x_loc, y_loc));
}

static void
CalculateNewSize(Widget w, Dimension *width_return, Dimension *height_return)
{
    SimpleMenuWidget xaw = (SimpleMenuWidget)w;
    Widget kid;
    Cardinal i;
    int width_kid, height_kid;
    int width, height, tmp_w, tmp_h, max_dim;
    short vadd, hadd;
    int n, columns, test_h, num_children = 0;
    Boolean try_layout = False;

#ifndef OLDXAW
    hadd = (short)(xaw->simple_menu.left_margin + xaw->simple_menu.right_margin);
#else
    hadd = 0;
#endif
    vadd = (short)(xaw->simple_menu.top_margin + xaw->simple_menu.bottom_margin);
    if (xaw->simple_menu.label)
	vadd = (short)(vadd + XtHeight(xaw->simple_menu.label));

    if (*height_return)
	max_dim = *height_return;
    else if (!XtHeight(w)) {
	max_dim = HeightOfScreen(XtScreen(w));
	try_layout = True;
    }
    else
	max_dim = XtHeight(w);
    max_dim -= vadd;

    width = height = tmp_w = tmp_h = n = test_h = 0;
    columns = 1;
    for (i = xaw->simple_menu.label ? 1 : 0;
	 i < xaw->composite.num_children;
	 i++) {
	kid = xaw->composite.children[i];
	if (!XtIsManaged(kid))
	    continue;
	++num_children;
	width_kid = XtWidth(kid);
	height_kid = XtHeight(kid);

	if (try_layout) {
	    if (!test_h)
		test_h = height_kid;
	    else if (test_h != height_kid)
		try_layout = False;
	}

	if (n && (height + height_kid > max_dim)) {
	    ++columns;
	    width += tmp_w;
	    tmp_w = width_kid;
	    height = height_kid;
	}
	else
	    height += height_kid;
	if (height > tmp_h)
	    tmp_h = height;
	if (width_kid > tmp_w)
	    tmp_w = width_kid;
	++n;
    }

    height = tmp_h + vadd;
    width += tmp_w + hadd;

    if (xaw->simple_menu.label)
	width = XawMax(width, XtWidth(xaw->simple_menu.label) + hadd);

    *width_return = (Dimension)width;
    *height_return = (Dimension)height;

    if (try_layout && columns > 1 && num_children > 2) {
	int space;

	height = test_h * (xaw->simple_menu.label ?
			   num_children - 1 :
			   num_children);

	max_dim -= max_dim % test_h;
	space = max_dim - (height % max_dim);
	if (space >= test_h * columns) {
	    height = max_dim - space / columns;
	    if (height % test_h)
		height += test_h - (height % test_h);
	    *height_return = (Dimension)(height + vadd);
	    CalculateNewSize(w, width_return, height_return);
	}
    }
}

static void
MakeResizeRequest(Widget w)
{
    int tries;
    Dimension width, height;

    width = XtWidth(w);
    height = XtHeight(w);

    for (tries = 0; tries < 100; tries++) {
	CalculateNewSize(w, &width, &height);
	if (width == XtWidth(w) && height == XtHeight(w))
	    break;
	if (XtMakeResizeRequest(w, width, height, &width, &height) ==
	    XtGeometryNo)
	break;
    }
}

#ifndef OLDXAW
static void
Popdown(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;

    while (XtParent(w) &&
	   XtIsSubclass(XtParent(w), simpleMenuWidgetClass)) {
	if (((SimpleMenuWidget)XtParent(w))->simple_menu.sub_menu == (Widget)w) {
	    w = XtParent(w);
	    smw = (SimpleMenuWidget)w;
	    smw->simple_menu.entry_set = NULL;
	}
	else
	    break;
    }

    smw->simple_menu.state |= SMW_UNMAPPING;
    if (smw->simple_menu.sub_menu)
	PopdownSubMenu(smw);
    XtCallActionProc(w, "XtMenuPopdown", event, params, *num_params);
}

static void
PopupSubMenu(SimpleMenuWidget smw)
{
    Arg args[2];
    Cardinal num_args;
    Widget menu;
    SmeBSBObject entry = (SmeBSBObject)smw->simple_menu.entry_set;
    Position menu_x, menu_y;
    Bool popleft;

    if (entry->sme_bsb.menu_name == NULL)
	return;

    if ((menu = FindMenu((Widget)smw, entry->sme_bsb.menu_name)) == NULL)
	return;

    smw->simple_menu.sub_menu = menu;

    if (!XtIsRealized(menu))
	XtRealizeWidget(menu);

    popleft = (smw->simple_menu.state & SMW_POPLEFT) != 0;

    if (popleft)
	XtTranslateCoords((Widget)smw,
			  (Position)(-(int)XtWidth(menu)),
			  (Position)(XtY(entry) - XtBorderWidth(menu)),
			  &menu_x, &menu_y);
    else
	XtTranslateCoords((Widget)smw,
			  (Position)XtWidth(smw),
			  (Position)(XtY(entry) - XtBorderWidth(menu)),
			  &menu_x, &menu_y);

    if (!popleft && menu_x >= 0) {
	int scr_width = WidthOfScreen(XtScreen(menu));

	if (menu_x + XtWidth(menu) > scr_width) {
	    menu_x = (Position)(menu_x - (XtWidth(menu) + XtWidth(smw)));
	    popleft = True;
	}
    }
    else if (popleft && menu_x < 0) {
	menu_x = 0;
	popleft = False;
    }
    if (menu_y >= 0) {
	int scr_height = HeightOfScreen(XtScreen(menu));

	if (menu_y + XtHeight(menu) > scr_height)
	    menu_y = (Position)(scr_height - XtHeight(menu) - XtBorderWidth(menu));
    }
    if (menu_y < 0)
	menu_y = 0;

    num_args = 0;
    XtSetArg(args[num_args], XtNx, menu_x);	num_args++;
    XtSetArg(args[num_args], XtNy, menu_y);	num_args++;
    XtSetValues(menu, args, num_args);

    if (popleft)
	((SimpleMenuWidget)menu)->simple_menu.state |= SMW_POPLEFT;
    else
	((SimpleMenuWidget)menu)->simple_menu.state &= (unsigned char)(~SMW_POPLEFT);

    XtPopup(menu, XtGrabNone);
}

static void
PopdownSubMenu(SimpleMenuWidget smw)
{
    SimpleMenuWidget menu = (SimpleMenuWidget)smw->simple_menu.sub_menu;

    if (!menu)
	return;

    menu->simple_menu.state |= SMW_UNMAPPING;
    PopdownSubMenu(menu);

    XtPopdown((Widget)menu);

    smw->simple_menu.sub_menu = NULL;
}

/*ARGSUSED*/
static void
PopupCB(Widget w, XtPointer client_data _X_UNUSED, XtPointer call_data _X_UNUSED)
{
    SimpleMenuWidget smw = (SimpleMenuWidget)w;

    smw->simple_menu.state &= (unsigned char)(~(SMW_UNMAPPING | SMW_POPLEFT));
}
#endif /* OLDXAW */
