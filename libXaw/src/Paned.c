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
 * Updated and significantly modified from the Athena VPaned Widget.
 *
 * Date:    March 1, 1989
 *
 * By:      Chris D. Peterson
 *          MIT X Consortium
 *          kit@expo.lcs.mit.edu
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/cursorfont.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/Grip.h>
#include <X11/Xaw/PanedP.h>
#include <X11/Xaw/XawImP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

typedef enum {
  UpLeftPane = 'U',
  LowRightPane = 'L',
  ThisBorderOnly = 'T',
  AnyPane = 'A'
} Direction;

#define NO_INDEX -100
#define IS_GRIP  NULL

#define PaneInfo(w)	((Pane)(w)->core.constraints)
#define HasGrip(w)	(PaneInfo(w)->grip != NULL)
#define IsPane(w)	((w)->core.widget_class != gripWidgetClass)
#define PaneIndex(w)	(PaneInfo(w)->position)
#define IsVert(w)	((w)->paned.orientation == XtorientVertical)

#define ForAllPanes(pw, childP) \
for ((childP) = (pw)->composite.children;				\
     (childP) < (pw)->composite.children + (pw)->paned.num_panes;	\
     (childP)++)

#define ForAllChildren(pw, childP) \
for ((childP) = (pw)->composite.children;				 \
     (childP) < (pw)->composite.children + (pw)->composite.num_children; \
     (childP)++)

#define PaneSize(paned, vertical)			\
     ((vertical) ? XtHeight(paned) : XtWidth(paned))

#define GetRequestInfo(geo, vertical)			\
     ((vertical) ? (geo)->height : (geo)->width)

#define SatisfiesRule1(pane, shrink)			\
     (((shrink) && ((pane)->size != (pane)->min))	\
      || (!(shrink) && ((pane)->size != (pane)->max)))

#define SatisfiesRule2(pane)					\
     (!(pane)->skip_adjust || (pane)->paned_adjusted_me)

#define SatisfiesRule3(pane, shrink)					\
     ((pane)->paned_adjusted_me						\
      && (((shrink) && ((int)(pane)->wp_size <= (pane)->size))		\
	  || (!(shrink) && ((int)(pane)->wp_size >= (pane)->size))))


/*
 * Class Methods
 */
static void XawPanedClassInitialize(void);
static void XawPanedChangeManaged(Widget);
static void XawPanedDeleteChild(Widget);
static void XawPanedDestroy(Widget);
static XtGeometryResult XawPanedGeometryManager(Widget, XtWidgetGeometry*,
						XtWidgetGeometry*);
static void XawPanedInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawPanedInsertChild(Widget);
static Boolean XawPanedPaneSetValues(Widget, Widget, Widget,
				     ArgList, Cardinal*);
static void XawPanedRealize(Widget, Mask*, XSetWindowAttributes*);
static void XawPanedRedisplay(Widget, XEvent*, Region);
static void XawPanedResize(Widget);
static Boolean XawPanedSetValues(Widget, Widget, Widget, ArgList, Cardinal*);

/*
 * Prototypes
 */
static void _DrawInternalBorders(PanedWidget, GC);
static void _DrawRect(PanedWidget, GC, int, int, unsigned int, unsigned int);
static void _DrawTrackLines(PanedWidget, Bool);
static void AdjustPanedSize(PanedWidget, unsigned int, XtGeometryResult*,
			    Dimension*, Dimension*);
static void ChangeAllGripCursors(PanedWidget);
static Pane ChoosePaneToResize(PanedWidget, int, Direction, Bool);
static void ClearPaneStack(PanedWidget);
static void CommitGripAdjustment(PanedWidget);
static void CreateGrip(Widget);
static int GetEventLocation(PanedWidget, XEvent*);
static void GetGCs(Widget);
static void GetPaneStack(PanedWidget, Bool, Pane*, int*);
static void HandleGrip(Widget, XtPointer, XtPointer);
static void LoopAndRefigureChildren(PanedWidget, int, Direction, int*);
static void ManageAndUnmanageGrips(PanedWidget);
static void MoveGripAdjustment(PanedWidget, Widget, Direction, int);
static Bool PopPaneStack(PanedWidget);
static void PushPaneStack(PanedWidget, Pane);
static void RefigureLocations(PanedWidget, int, Direction);
static void RefigureLocationsAndCommit(Widget);
static void ReleaseGCs(Widget);
static void ResortChildren(PanedWidget);
static void SetChildrenPrefSizes(PanedWidget, unsigned int);
static void StartGripAdjustment(PanedWidget, Widget, Direction);

/*
 * Initialization
 */
static char defGripTranslations[] =
"<Btn1Down>:"		"GripAction(Start,UpLeftPane)\n"
"<Btn2Down>:"		"GripAction(Start,ThisBorderOnly)\n"
"<Btn3Down>:"		"GripAction(Start,LowRightPane)\n"
"<Btn1Motion>:"		"GripAction(Move,UpLeft)\n"
"<Btn2Motion>:"		"GripAction(Move,ThisBorder)\n"
"<Btn3Motion>:"		"GripAction(Move,LowRight)\n"
"Any<BtnUp>:"		"GripAction(Commit)\n"
;

#define offset(field) XtOffsetOf(PanedRec, paned.field)
static XtResource resources[] = {
  {
    XtNinternalBorderColor,
    XtCBorderColor,
    XtRPixel,
    sizeof(Pixel),
    offset(internal_bp),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNinternalBorderWidth,
    XtCBorderWidth,
    XtRDimension,
    sizeof(Dimension),
    offset(internal_bw),
    XtRImmediate,
    (XtPointer)1
  },
  {
    XtNgripIndent,
    XtCGripIndent,
    XtRPosition,
    sizeof(Position),
    offset(grip_indent),
    XtRImmediate,
    (XtPointer)10
  },
  {
    XtNrefigureMode,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(refiguremode),
    XtRImmediate,
    (XtPointer)True
  },
  {
    XtNgripTranslations,
    XtCTranslations,
    XtRTranslationTable,
    sizeof(XtTranslations),
    offset(grip_translations),
    XtRString,
    (XtPointer)defGripTranslations
  },
  {
    XtNorientation,
    XtCOrientation,
    XtROrientation,
    sizeof(XtOrientation),
    offset(orientation),
    XtRImmediate,
    (XtPointer)XtorientVertical
  },
  {
    XtNcursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(cursor),
    XtRImmediate,
    NULL
  },
  {
    XtNgripCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(grip_cursor),
    XtRImmediate,
    NULL
  },
  {
    XtNverticalGripCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(v_grip_cursor),
    XtRString,
    (XtPointer)"sb_v_double_arrow"
  },
  {
    XtNhorizontalGripCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(h_grip_cursor),
    XtRString,
    (XtPointer)"sb_h_double_arrow"
  },
  {
    XtNbetweenCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(adjust_this_cursor),
    XtRString,
    NULL
  },
  {
    XtNverticalBetweenCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(v_adjust_this_cursor),
    XtRString,
    (XtPointer)"sb_left_arrow"
  },
  {
    XtNhorizontalBetweenCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(h_adjust_this_cursor),
    XtRString,
    (XtPointer)"sb_up_arrow"
  },
  {
    XtNupperCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(adjust_upper_cursor),
    XtRString,
    (XtPointer)"sb_up_arrow"
  },
  {
    XtNlowerCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(adjust_lower_cursor),
    XtRString,
    (XtPointer)"sb_down_arrow"
  },
  {
    XtNleftCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(adjust_left_cursor),
    XtRString,
    (XtPointer)"sb_left_arrow"
  },
  {
    XtNrightCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    offset(adjust_right_cursor),
    XtRString,
    (XtPointer)"sb_right_arrow"
  },
};
#undef offset

#define offset(field) XtOffsetOf(PanedConstraintsRec, paned.field)
static XtResource subresources[] = {
  {
    XtNallowResize,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(allow_resize),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNposition,
    XtCPosition,
    XtRInt,
    sizeof(int),
    offset(position),
    XtRImmediate,
    (XtPointer)0
  },
  {
    XtNmin,
    XtCMin,
    XtRDimension,
    sizeof(Dimension),
    offset(min),
    XtRImmediate,
    (XtPointer)PANED_GRIP_SIZE
  },
  {
    XtNmax,
    XtCMax,
    XtRDimension,
    sizeof(Dimension),
    offset(max),
    XtRImmediate,
    (XtPointer)~0
  },
  {
    XtNpreferredPaneSize,
    XtCPreferredPaneSize,
    XtRDimension,
    sizeof(Dimension),
    offset(preferred_size),
    XtRImmediate,
    (XtPointer)PANED_ASK_CHILD
  },
  {
    XtNresizeToPreferred,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(resize_to_pref),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNskipAdjust,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(skip_adjust),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNshowGrip,
    XtCShowGrip,
    XtRBoolean,
    sizeof(Boolean),
    offset(show_grip),
    XtRImmediate,
    (XtPointer)True
  },
};
#undef offset

#define SuperClass ((ConstraintWidgetClass)&constraintClassRec)

PanedClassRec panedClassRec = {
   /* core */
   {
    (WidgetClass)SuperClass,		/* superclass */
    "Paned",				/* class name */
    sizeof(PanedRec),			/* size */
    XawPanedClassInitialize,		/* class_initialize */
    NULL,				/* class_part init */
    False,				/* class_inited */
    XawPanedInitialize,			/* initialize */
    NULL,				/* initialize_hook */
    XawPanedRealize,			/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawPanedDestroy,			/* destroy */
    XawPanedResize,			/* resize */
    XawPanedRedisplay,			/* expose */
    XawPanedSetValues,			/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
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
    XawPanedGeometryManager,		/* geometry_manager */
    XawPanedChangeManaged,		/* change_managed */
    XawPanedInsertChild,		/* insert_child */
    XawPanedDeleteChild,		/* delete_child */
    NULL,				/* extension */
  },
  /* constraint */
  {
    subresources,			/* subresources */
    XtNumber(subresources),		/* subresource_count */
    sizeof(PanedConstraintsRec),	/* constraint_size */
    NULL,				/* initialize */
    NULL,				/* destroy */
    XawPanedPaneSetValues,		/* set_values */
    NULL,				/* extension */
  },
};

WidgetClass panedWidgetClass = (WidgetClass)&panedClassRec;
WidgetClass vPanedWidgetClass = (WidgetClass)&panedClassRec;

/*
 * Implementation
 */
/* Function:
 *	AdjustPanedSize
 *
 * Parameters:
 *	pw	     - paned widget to adjust
 *	off_size     - new off_size to use
 *	result_ret   - result of query (return)
 *	on_size_ret  - new on_size  (return)
 *	off_size_ret - new off_size (return)
 *
 * Description:
 *	Adjusts the size of the pane.
 *
 * Returns:
 *	amount of change in size
 */
static void
AdjustPanedSize(PanedWidget pw, unsigned int off_size,
		XtGeometryResult *result_ret,
		Dimension *on_size_ret, Dimension *off_size_ret)
{
    Dimension old_size = PaneSize((Widget)pw, IsVert(pw));
    Dimension newsize = 0;
    Widget *childP;
    XtWidgetGeometry request, reply;

    request.request_mode = CWWidth | CWHeight;

    ForAllPanes(pw, childP) {
	int size = Max(PaneInfo(*childP)->size, (int)PaneInfo(*childP)->min);

	AssignMin(size, (int)PaneInfo(*childP)->max);
	newsize = (newsize + (size + pw->paned.internal_bw));
    }
    newsize = (Dimension)(newsize - pw->paned.internal_bw);

    if (newsize < 1)
	newsize = 1;

    if (IsVert(pw)) {
	request.width = (Dimension)off_size;
	request.height = newsize;
    }
    else {
	request.width = newsize;
	request.height = (Dimension)off_size;
    }

    if (result_ret != NULL) {
	request.request_mode |= XtCWQueryOnly;

	*result_ret = XtMakeGeometryRequest((Widget)pw, &request, &reply);
	_XawImCallVendorShellExtResize((Widget)pw);

	if (newsize == old_size || *result_ret == XtGeometryNo) {
	    *on_size_ret = old_size;
	    *off_size_ret = (Dimension)off_size;
	    return;
	}
	if (*result_ret != XtGeometryAlmost) {
	    *on_size_ret = GetRequestInfo(&request, IsVert(pw));
	    *off_size_ret = GetRequestInfo(&request, !IsVert(pw));
	    return;
	}
	*on_size_ret = GetRequestInfo(&reply, IsVert(pw));
	*off_size_ret = GetRequestInfo(&reply, !IsVert(pw));
	return;
    }

    if (newsize == old_size)
	return;

    if (XtMakeGeometryRequest((Widget)pw, &request, &reply) == XtGeometryAlmost)
	XtMakeGeometryRequest((Widget)pw, &reply, &request);
}

/*
 * Function:
 *	ChoosePaneToResize.
 *
 * Parameters:
 *	pw	  - paned widget
 *	paneindex - index of the current pane
 *	dir	  - direction to search first
 *	shrink	  - True if we need to shrink a pane, False otherwise
 *
 * Description:
 *	  This function chooses a pane to resize.
 *	  They are chosen using the following rules:
 *
 *		   1) size < max && size > min
 *	2) skip adjust == False
 *	3) widget not its preferred height
 *	   && this change will bring it closer
 *	   && The user has not resized this pane.
 *
 *		   If no widgets are found that fits all the rules then
 *		      rule #3 is broken.
 *		   If there are still no widgets found than
 *		      rule #2 is broken.
 *		   Rule #1 is never broken.
 *		   If no widgets are found then NULL is returned.
 *
 * Returns:
 *	pane to resize or NULL
 */
static Pane
ChoosePaneToResize(PanedWidget pw, int paneindex, Direction dir, Bool shrink)
{
    Widget *childP;
    int rules = 3;
    Direction _dir = dir;
    int _index = paneindex;

    if (paneindex == NO_INDEX || dir == AnyPane) {		/* Use defaults */
	_dir = LowRightPane;			/* Go up - really */
	_index = pw->paned.num_panes - 1;	/* Start the last pane, and work
						   backwards */
    }
    childP = pw->composite.children + _index;

    /*CONSTCOND*/
    while(True) {
	Pane pane = PaneInfo(*childP);

	if ((rules < 3 || SatisfiesRule3(pane, shrink))
	    && (rules < 2 || SatisfiesRule2(pane))
	    && SatisfiesRule1(pane, shrink)
	    && (paneindex != PaneIndex(*childP) || dir == AnyPane))
	    return (pane);

	/*
	 * This is counter-intuitive, but if we are resizing the pane
	 * above the grip we want to choose a pane below the grip to lose,
	 * and visa-versa
	 */
	if (_dir == LowRightPane)
	    --childP;
	else
	    ++childP;

	/*
	 * If we have come to and edge then reduce the rule set, and try again
	 * If we are reduced the rules to none, then return NULL
	 */
	if ((childP - pw->composite.children) < 0 ||
	    (childP - pw->composite.children) >= pw->paned.num_panes) {
	    if (--rules < 1)	/* less strict rules */
		return (NULL);
	    childP = pw->composite.children + _index;
	}
    }
}

/*
 * Function:
 *	LoopAndRefigureChildren
 *
 * Parameters:
 *	pw	  - paned widget
 *	paneindex - number of the pane border we are moving
 *	dir	  - pane to move (either UpLeftPane or LowRightPane)
 *	sizeused  - current amount of space used (used and returned)
 *
 * Description:
 *	  If we are resizing either the UpleftPane or LowRight Pane loop
 *	through all the children to see if any will allow us to resize them.
 */
static void
LoopAndRefigureChildren(PanedWidget pw, int paneindex, Direction dir,
			int *sizeused)
{
    int pane_size = (int)PaneSize((Widget)pw, IsVert(pw));
    Boolean shrink = (*sizeused > pane_size);

    if (dir == LowRightPane)
	paneindex++;

    /* While all panes do not fit properly */
    while (*sizeused != pane_size) {
	/*
	 * Choose a pane to resize
	 * First look on the Pane Stack, and then go hunting for another one
	 * If we fail to find a pane to resize then give up
	 */
	Pane pane;
	int start_size;
	Dimension old;
	Boolean rule3_ok = False, from_stack = True;

	GetPaneStack(pw, shrink, &pane, &start_size);
	if (pane == NULL) {
	    pane = ChoosePaneToResize(pw, paneindex, dir, shrink);
	    if (pane == NULL)
		return; /* no one to resize, give up */

	    rule3_ok = SatisfiesRule3(pane, shrink);
	    from_stack = False;
	    PushPaneStack(pw, pane);
	}

	/*
	 * Try to resize this pane so that all panes will fit, take min and max
	 * into account
	 */
	old = (Dimension) pane->size;
	pane->size += pane_size - *sizeused;

	if (from_stack) {
	    if (shrink) {
		AssignMax(pane->size, start_size);
	    }	/* don't remove these braces */
	    else
		AssignMin(pane->size, start_size);

	  if (pane->size == start_size)
	    (void)PopPaneStack(pw);
	}
	else if (rule3_ok) {
	    if (shrink) {
		AssignMax(pane->size, (int)pane->wp_size);
	    }	/* don't remove these braces */
	    else
		AssignMin(pane->size, (int)pane->wp_size);
	}

	pane->paned_adjusted_me = pane->size != pane->wp_size;
	AssignMax(pane->size, (int)pane->min);
	AssignMin(pane->size, (int)pane->max);
	*sizeused += (pane->size - old);
    }
}

/*
 * Function:
 *	RefigureLocations
 *
 * Parameters:
 *	pw	  - paned widget
 *	paneindex - child to start refiguring at
 *	dir	  - direction to move from child
 *
 * Description:
 *	  Refigures all locations of children.
 *      There are special arguments to paneindex and dir, they are:
 *      paneindex - NO_INDEX.
 *      dir   - AnyPane.
 *
 *      If either of these is true then all panes may be resized and
 *      the choosing of panes proceeds in reverse order starting with the
 *      last child.
 */
static void
RefigureLocations(PanedWidget pw, int paneindex, Direction dir)
{
    Widget *childP;
    int pane_size = (int)PaneSize((Widget)pw, IsVert(pw));
    int sizeused = 0;
    Position loc = 0;

    if (pw->paned.num_panes == 0 || !pw->paned.refiguremode)
	return;

    /*
     * Get an initial estimate of the size we will use
     */
    ForAllPanes(pw, childP) {
	Pane pane = PaneInfo(*childP);

	AssignMax(pane->size, (int) pane->min);
	AssignMin(pane->size, (int) pane->max);
	sizeused += (int)pane->size + (int)pw->paned.internal_bw;
    }
    sizeused -= (int)pw->paned.internal_bw;

    if (dir != ThisBorderOnly && sizeused != pane_size)
	LoopAndRefigureChildren(pw, paneindex, dir, &sizeused);

    /*
     * If we still are not the right size, then tell the pane that
     * wanted to resize that it can't
     */
    if (paneindex != NO_INDEX && dir != AnyPane) {
	Pane pane = PaneInfo(*(pw->composite.children + paneindex));
	Dimension old = (Dimension)pane->size;

	pane->size += pane_size - sizeused;
	AssignMax(pane->size, (int) pane->min);
	AssignMin(pane->size, (int) pane->max);
	sizeused += pane->size - old;
    }

    /*
     * It is possible that the panes will not fit inside the vpaned widget, but
     * we have tried out best
     *
     * Assign each pane a location
     */
    ForAllPanes(pw, childP) {
	PaneInfo(*childP)->delta = loc;
	loc = (Position)(loc + (PaneInfo(*childP)->size + pw->paned.internal_bw));
    }
}

/*
 * Function:
 *	CommitNewLocations
 *
 * Parameters:
 *	pw - paned widget
 *
 * Description:
 *	Commits all of the previously figured locations.
 */
static void
CommitNewLocations(PanedWidget pw)
{
    Widget *childP;
    XWindowChanges changes;

    changes.stack_mode = Above;

    ForAllPanes(pw, childP) {
	Pane pane = PaneInfo(*childP);
	Widget grip = pane->grip;	/* may be NULL */

	if (IsVert(pw)) {
	    XtMoveWidget(*childP, (Position) 0, pane->delta);
	    XtResizeWidget(*childP, XtWidth(pw), (Dimension)pane->size, 0);

	    if (HasGrip(*childP)) {	/* Move and Display the Grip */
		changes.x = XtWidth(pw) - pw->paned.grip_indent -
			    XtWidth(grip) - (XtBorderWidth(grip) << 1);
		changes.y = XtY(*childP) + XtHeight(*childP) -
			    (XtHeight(grip) >> 1) - XtBorderWidth(grip) +
			    (pw->paned.internal_bw >> 1);
	    }
	}
	else {
	    XtMoveWidget(*childP, pane->delta, 0);
	    XtResizeWidget(*childP, (Dimension)pane->size, (Dimension)XtHeight(pw), 0);

	    if (HasGrip(*childP)) {		/* Move and Display the Grip */
		changes.x = XtX(*childP) + XtWidth(*childP) -
			    (XtWidth(grip) >> 1) - XtBorderWidth(grip) +
			    (pw->paned.internal_bw >> 1);
		changes.y = XtHeight(pw) - pw->paned.grip_indent -
			    XtHeight(grip) - (XtBorderWidth(grip) << 1);
	    }
	}

	/*
	 * This should match XtMoveWidget, except that we're also insuring the
	 * grip is Raised in the same request
	 */

	if (HasGrip(*childP)) {
	    XtX(grip) = (Position)changes.x;
	    XtY(grip) = (Position)changes.y;

	    if (XtIsRealized(pane->grip))
		XConfigureWindow(XtDisplay(pane->grip), XtWindow(pane->grip),
				 CWX | CWY | CWStackMode, &changes);
	}
    }
    ClearPaneStack(pw);
}

/*
 * Function:
 *	RefigureLocationsAndCommit
 *
 * Parameters:
 *	pw - paned widget
 *
 * Description:
 *	Refigures all locations in a paned widget and commits them immediately.
 *
 *      This function does nothing if any of the following are true.
 *      o refiguremode is false.
 *      o The widget is unrealized.
 *      o There are no panes is the paned widget.
 */
static void
RefigureLocationsAndCommit(Widget w)
{
    PanedWidget pw = (PanedWidget)w;

    if (pw->paned.refiguremode && XtIsRealized(w) && pw->paned.num_panes > 0) {
	RefigureLocations(pw, NO_INDEX, AnyPane);
	CommitNewLocations(pw);
    }
}

/*
 * Function:
 *	_DrawRect
 *
 * Parameters:
 *	pw	 - paned widget
 *	gc	 - gc to used for the draw
 *	on_olc	 - location of upper left corner of rect
 *	off_loc	 - ""
 *	on_size	 - size of rectangle
 *	off_size - ""
 *
 * Description:
 *	Draws a rectangle in the proper orientation.
 */
static void
_DrawRect(PanedWidget pw, GC gc, int on_loc, int off_loc,
	  unsigned int on_size, unsigned int off_size)
{
    if (IsVert(pw))
	XFillRectangle(XtDisplay((Widget)pw), XtWindow((Widget)pw), gc,
		       off_loc, on_loc, off_size, on_size);
    else
	XFillRectangle(XtDisplay((Widget)pw), XtWindow((Widget)pw), gc,
		       on_loc, off_loc, on_size, off_size);
}

/*
 * Function:
 *	_DrawInternalBorders
 *
 * Parameters:
 *	pw - paned widget
 *	gc - GC to use to draw the borders
 *
 * Description:
 *	Draws the internal borders into the paned widget.
 */
static void
_DrawInternalBorders(PanedWidget pw, GC gc)
{
    Widget *childP;
    int on_loc, off_loc;
    unsigned int on_size, off_size;

    /*
     * This is an optimization.  Do not paint the internal borders if
     * they are the same color as the background
     */
    if (pw->core.background_pixel == pw->paned.internal_bp)
	return;

    off_loc = 0;
    off_size = (unsigned int) PaneSize((Widget)pw, !IsVert(pw));
    on_size = (unsigned int)pw->paned.internal_bw;

    ForAllPanes(pw, childP) {
	on_loc = IsVert(pw) ? XtY(*childP) : XtX(*childP);
	on_loc -= (int)on_size;

	_DrawRect(pw, gc, on_loc, off_loc, on_size, off_size);
    }
}

#define DrawInternalBorders(pw)				\
	_DrawInternalBorders((pw), (pw)->paned.normgc)
#define EraseInternalBorders(pw)			\
	_DrawInternalBorders((pw), (pw)->paned.invgc)
/*
 * Function Name:
 *	_DrawTrackLines
 *
 * Parameters:
 *	pw - Paned widget
 *	erase - if True then just erase track lines, else draw them in
 *
 * Description:
 *	Draws the lines that animate the pane borders when the grips are moved.
 */
static void
_DrawTrackLines(PanedWidget pw, Bool erase)
{
    Widget *childP;
    Pane pane;
    int on_loc, off_loc;
    unsigned int on_size, off_size;

    off_loc = 0;
    off_size = PaneSize((Widget)pw, !IsVert(pw));

    ForAllPanes(pw, childP) {
	pane = PaneInfo(*childP);
	if (erase || pane->olddelta != pane->delta) {
	    on_size = pw->paned.internal_bw;
	    if (!erase) {
		on_loc = PaneInfo(*childP)->olddelta - (int) on_size;
		_DrawRect(pw, pw->paned.flipgc,
			  on_loc, off_loc, on_size, off_size);
	    }

	    on_loc = PaneInfo(*childP)->delta - (int)on_size;

	    _DrawRect(pw, pw->paned.flipgc,
		      on_loc, off_loc, on_size, off_size);

	    pane->olddelta = pane->delta;
	}
    }
}

#define DrawTrackLines(pw)	_DrawTrackLines((pw), False);
#define EraseTrackLines(pw)	_DrawTrackLines((pw), True);
/*
 * Function:
 *	GetEventLocation
 *
 * Parameters:
 *	pw    - the paned widget
 *	event - pointer to an event
 *
 * Description:
 *	Converts and event to an x and y location.
 *
 * Returns:
 *	if this is a vertical pane then (y) else (x)
 */
static int
GetEventLocation(PanedWidget pw, XEvent *event)
{
    int x, y;

    switch (event->xany.type) {
	case ButtonPress:
	case ButtonRelease:
	    x = event->xbutton.x_root;
	    y = event->xbutton.y_root;
	    break;
	case KeyPress:
	case KeyRelease:
	    x = event->xkey.x_root;
	    y = event->xkey.y_root;
	    break;
	case MotionNotify:
	    x = event->xmotion.x_root;
	    y = event->xmotion.y_root;
	    break;
	default:
	    x = pw->paned.start_loc;
	    y = pw->paned.start_loc;
    }

    if (IsVert(pw))
	return (y);

  return (x);
}

/*
 * Function:
 *	StartGripAdjustment
 *
 * Parameters:
 *	pw   - paned widget
 *	grip - grip widget selected
 *	dir  - direction that we are to be moving
 *
 * Description:
 *	Starts the grip adjustment procedure.
 */
static void
StartGripAdjustment(PanedWidget pw, Widget grip, Direction dir)
{
    Widget *childP;
    Cursor cursor;

    pw->paned.whichadd = pw->paned.whichsub = NULL;

    if (dir == ThisBorderOnly || dir == UpLeftPane)
	pw->paned.whichadd = pw->composite.children[PaneIndex(grip)];
    if (dir == ThisBorderOnly || dir == LowRightPane)
	pw->paned.whichsub = pw->composite.children[PaneIndex(grip) + 1];

    /*
     * Change the cursor
     */
    if (XtIsRealized(grip)) {
	if (IsVert(pw)) {
	    if (dir == UpLeftPane)
		cursor = pw->paned.adjust_upper_cursor;
	    else if (dir == LowRightPane)
		cursor = pw->paned.adjust_lower_cursor;
	    else {
		if (pw->paned.adjust_this_cursor == None)
		    cursor = pw->paned.v_adjust_this_cursor;
		else
		    cursor = pw->paned.adjust_this_cursor;
	    }
	}
	else {
	    if (dir == UpLeftPane)
		cursor = pw->paned.adjust_left_cursor;
	    else if (dir == LowRightPane)
		cursor = pw->paned.adjust_right_cursor;
	    else {
		if (pw->paned.adjust_this_cursor == None)
		    cursor = pw->paned.h_adjust_this_cursor;
		else
		    cursor = pw->paned.adjust_this_cursor;
	    }
	}

	XDefineCursor(XtDisplay(grip), XtWindow(grip), cursor);
    }

    EraseInternalBorders(pw);
    ForAllPanes(pw, childP)
	PaneInfo(*childP)->olddelta = -99;

    EraseTrackLines(pw);
}

/*
 * Function:
 *	MoveGripAdjustment
 *
 * Parameters:
 *	pw   - paned widget
 *	grip - grip that we are moving
 *	dir  - direction the pane we are interested is w.r.t the grip
 *	loc  - location of pointer in proper direction
 *
 * Description:
 *	This routine moves all panes around when a grip is moved.
 */
static void
MoveGripAdjustment(PanedWidget pw, Widget grip, Direction dir, int loc)
{
    int diff, add_size = 0, sub_size = 0;

    diff = loc - pw->paned.start_loc;

    if (pw->paned.whichadd)
    add_size = PaneSize(pw->paned.whichadd, IsVert(pw)) + diff;

    if (pw->paned.whichsub)
    sub_size = PaneSize(pw->paned.whichsub, IsVert(pw)) - diff;

    /*
     * If moving this border only then do not allow either of the borders
     * to go beyond the min or max size allowed
     */
    if (dir == ThisBorderOnly) {
	int old_add_size = add_size, old_sub_size;

	AssignMax(add_size, (int)PaneInfo(pw->paned.whichadd)->min);
	AssignMin(add_size, (int)PaneInfo(pw->paned.whichadd)->max);
	if (add_size != old_add_size)
	    sub_size += old_add_size - add_size;

	old_sub_size = sub_size;
	AssignMax(sub_size, (int)PaneInfo(pw->paned.whichsub)->min);
	AssignMin(sub_size, (int)PaneInfo(pw->paned.whichsub)->max);
	if (sub_size != old_sub_size)
	    return;	/* Abort to current sizes */
    }

    if (add_size != 0)
	PaneInfo(pw->paned.whichadd)->size = add_size;
    if (sub_size != 0)
	PaneInfo(pw->paned.whichsub)->size = sub_size;
    RefigureLocations(pw, PaneIndex(grip), dir);
    DrawTrackLines(pw);
}

/*
 * Function:
 *	CommitGripAdjustment
 *
 * Parameters:
 *	pw - paned widget
 *
 * Description:
 *	Commits the grip adjustment.
 */
static void
CommitGripAdjustment(PanedWidget pw)
{
    EraseTrackLines(pw);
    CommitNewLocations(pw);
    DrawInternalBorders(pw);

    /*
     * Since the user selected this size then use it as the preferred size
     */
    if (pw->paned.whichadd) {
	Pane pane = PaneInfo(pw->paned.whichadd);

	pane->wp_size = (Dimension)pane->size;
    }
    if (pw->paned.whichsub) {
	Pane pane = PaneInfo(pw->paned.whichsub);

	pane->wp_size = (Dimension)pane->size;
    }
}

/*
 * Function:
 *	HandleGrip
 *
 * Parameters:
 *	grip	  - grip widget that has been moved
 *	temp	  - (not used)
 *	call_data - data passed to us from the grip widget
 *
 * Description:
 *	Handles the grip manipulations.
 */
/*ARGSUSED*/
static void
HandleGrip(Widget grip, XtPointer temp _X_UNUSED, XtPointer callData)
{
    XawGripCallData call_data = (XawGripCallData)callData;
    PanedWidget pw = (PanedWidget) XtParent(grip);
    int loc;
    char action_type[2], direction[2];
    Cursor cursor;
    Arg arglist[1];

    if (call_data->num_params)
	XmuNCopyISOLatin1Uppered(action_type, call_data->params[0],
				 sizeof(action_type));

    if (call_data->num_params == 0
	|| (action_type[0] == 'C' && call_data->num_params != 1)
	|| (action_type[0] != 'C' && call_data->num_params != 2))
	XtAppError(XtWidgetToApplicationContext(grip),
		   "Paned GripAction has been passed incorrect parameters.");

    loc = GetEventLocation(pw, (XEvent *)call_data->event);

    if (action_type[0] != 'C')
	XmuNCopyISOLatin1Uppered(direction, call_data->params[1],
				 sizeof(direction));

    switch (action_type[0]) {
	case 'S':		/* Start adjustment */
	    pw->paned.resize_children_to_pref = False;
	    StartGripAdjustment(pw, grip, (Direction)direction[0]);
	    pw->paned.start_loc = loc;
	    break;
	case 'M':
	    MoveGripAdjustment(pw, grip, (Direction)direction[0], loc);
	    break;
	case 'C':
	    XtSetArg(arglist[0], XtNcursor, &cursor);
	    XtGetValues(grip, arglist, 1);
	    XDefineCursor(XtDisplay(grip), XtWindow(grip), cursor);
	    CommitGripAdjustment(pw);
	    break;
	default:
	    XtAppError(XtWidgetToApplicationContext(grip),
		       "Paned GripAction(); 1st parameter invalid");
	    break;
     }
}

/*
 * Function:
 *	ResortChildren
 *
 * Arguments:
 *	pw - paned widget
 *
 * Description:
 *	Resorts the children so that all managed children are first.
 */
static void
ResortChildren(PanedWidget pw)
{
    Widget *unmanagedP, *childP;

    unmanagedP = NULL;
    ForAllChildren(pw, childP) {
	if (!IsPane(*childP) || !XtIsManaged(*childP)) {
	    /*
	     * We only keep track of the first unmanaged pane
	     */
	    if (unmanagedP == NULL)
		unmanagedP = childP;
	}
	else {			/* must be a managed pane */
	    /*
	     * If an earlier widget was not a managed pane, then swap
	     */
	    if (unmanagedP != NULL) {
		Widget child = *unmanagedP;

		*unmanagedP = *childP;
		*childP = child;
		childP = unmanagedP;  /* easiest to just back-track */
		unmanagedP = NULL;    /* in case there is another managed */
	   }
       }
   }
}

/*
 * Function:
 *	ManageAndUnmanageGrips
 *
 * Parameters:
 *	pw - paned widget
 *
 * Description:
 *	  This function manages and unmanages the grips so that
 *		   the managed state of each grip matches that of its pane.
 */
static void
ManageAndUnmanageGrips(PanedWidget pw)
{
    WidgetList managed_grips, unmanaged_grips;
    Widget *managedP, *unmanagedP, *childP;
    Cardinal alloc_size;

    alloc_size = (Cardinal)(sizeof(Widget) * (pw->composite.num_children >> 1));
    managedP = managed_grips = (WidgetList)XtMalloc(alloc_size);
    unmanagedP = unmanaged_grips = (WidgetList)XtMalloc(alloc_size);

    ForAllChildren(pw, childP)
	if (IsPane(*childP) && HasGrip(*childP)) {
	    if (XtIsManaged(*childP))
		*managedP++ = PaneInfo(*childP)->grip;
	    else
		*unmanagedP++ = PaneInfo(*childP)->grip;
	}

    if (managedP != managed_grips) {
	*unmanagedP++ = *--managedP;   /* Last grip is never managed */
	XtManageChildren(managed_grips, (Cardinal)(managedP - managed_grips));
    }

    if (unmanagedP != unmanaged_grips)
	XtUnmanageChildren(unmanaged_grips, (Cardinal)(unmanagedP - unmanaged_grips));

    XtFree((char *)managed_grips);
    XtFree((char *)unmanaged_grips);
}

/*
 * Function:
 *	CreateGrip
 *
 * Parameters:
 *	child - child that wants a grip to be created for it
 *
 * Description:
 *	Creates a grip widget.
 */
static void
CreateGrip(Widget child)
{
    PanedWidget pw = (PanedWidget)XtParent(child);
    Arg arglist[2];
    Cardinal num_args = 0;
    Cursor cursor;

    XtSetArg(arglist[num_args], XtNtranslations, pw->paned.grip_translations);
    num_args++;
    if ((cursor = pw->paned.grip_cursor) == None) {
	if (IsVert(pw))
	    cursor = pw->paned.v_grip_cursor;
	else
	    cursor = pw->paned.h_grip_cursor;
    }

    XtSetArg(arglist[num_args], XtNcursor, cursor);
    num_args++;
    PaneInfo(child)->grip = XtCreateWidget("grip", gripWidgetClass, (Widget)pw,
					   arglist, num_args);

    XtAddCallback(PaneInfo(child)->grip, XtNcallback,
		  HandleGrip, (XtPointer)child);
}

/*
 * Function:
 *	GetGCs
 *
 * Parameters:
 *	w - paned widget
 */
static void
GetGCs(Widget w)
{
    PanedWidget pw = (PanedWidget)w;
    XtGCMask valuemask;
    XGCValues values;

    /*
     * Draw pane borders in internal border color
     */
    values.foreground = pw->paned.internal_bp;
    valuemask = GCForeground;
    pw->paned.normgc = XtGetGC(w, valuemask, &values);

    /*
     * Erase pane borders with background color
     */
    values.foreground = pw->core.background_pixel;
    valuemask = GCForeground;
    pw->paned.invgc = XtGetGC(w, valuemask, &values);

    /*
     * Draw Track lines (animate pane borders) in
     * internal border color ^ bg color
     */
    values.function = GXinvert;
    values.plane_mask = pw->paned.internal_bp ^ pw->core.background_pixel;
    values.subwindow_mode = IncludeInferiors;
    valuemask = GCPlaneMask | GCFunction | GCSubwindowMode;
    pw->paned.flipgc = XtGetGC(w, valuemask, &values);
}

/*
 * Function:
 *	SetChildrenPrefSizes
 *
 * Parameters:
 *	pw - paned widget
 *
 * Description:
 *	Sets the preferred sizes of the children.
 */
static void
SetChildrenPrefSizes(PanedWidget pw, unsigned int off_size)
{
    Widget *childP;
    Boolean vert = IsVert(pw);
    XtWidgetGeometry request, reply;

    ForAllPanes(pw, childP)
	if (pw->paned.resize_children_to_pref || PaneInfo(*childP)->size == 0 ||
	    PaneInfo(*childP)->resize_to_pref) {
	    if (PaneInfo(*childP)->preferred_size != PANED_ASK_CHILD)
		PaneInfo(*childP)->wp_size = PaneInfo(*childP)->preferred_size;
	    else {
		if(vert) {
		    request.request_mode = CWWidth;
		    request.width = (Dimension) off_size;
		}
		else {
		    request.request_mode = CWHeight;
		    request.height = (Dimension) off_size;
		}

		if ((XtQueryGeometry(*childP, &request, &reply)
		     == XtGeometryAlmost)
		    && (reply.request_mode = (vert ? CWHeight : CWWidth)))
		    PaneInfo(*childP)->wp_size = GetRequestInfo(&reply, vert);
		else
		    PaneInfo(*childP)->wp_size = PaneSize(*childP, vert);
	    }

	    PaneInfo(*childP)->size = PaneInfo(*childP)->wp_size;
	}
}

/*
 * Function:
 *	ChangeAllGripCursors
 *
 * Parameters:
 *	pw - paned widget
 *
 *	Description:
 *	Changes all the grip cursors.
 */
static void
ChangeAllGripCursors(PanedWidget pw)
{
    Widget *childP;

    ForAllPanes(pw, childP) {
	Arg arglist[1];
	Cursor cursor;

	if ((cursor = pw->paned.grip_cursor) == None) {
	    if (IsVert(pw))
		cursor = pw->paned.v_grip_cursor;
	    else
		cursor = pw->paned.h_grip_cursor;
	}

	if (HasGrip(*childP)) {
	    XtSetArg(arglist[0], XtNcursor, cursor);
	    XtSetValues(PaneInfo(*childP)->grip, arglist, 1);
	}
    }
}

/*
 * Function:
 *	PushPaneStack
 *
 * Parameters:
 *	pw   - paned widget
 *	pane - pane that we are pushing
 *
 * Description:
 *	Pushes a value onto the pane stack.
 */
static void
PushPaneStack(PanedWidget pw, Pane pane)
{
    PaneStack *stack = (PaneStack *)XtMalloc(sizeof(PaneStack));

    stack->next = pw->paned.stack;
    stack->pane = pane;
    stack->start_size = pane->size;

    pw->paned.stack = stack;
}

/*
 * Function:
 *	GetPaneStack
 *
 * Parameters:
 *	pw - paned widget
 *	shrink	   - True if we want to shrink this pane, False otherwise
 *	pane	   - pane that we are popping (return)
 *	start_size - size that this pane started at (return)
 *
 * Description:
 *	Gets the top value from the pane stack.
 */
static void
GetPaneStack(PanedWidget pw, Bool shrink, Pane *pane, int *start_size)
{
    if (pw->paned.stack == NULL) {
	*pane = NULL;
	return;
    }

    *pane = pw->paned.stack->pane;
    *start_size = pw->paned.stack->start_size;

    if (shrink != ((*pane)->size > *start_size))
	*pane = NULL;
}

/*
 * Function:
 *	PopPaneStack
 *
 * Parameters:
 *	pw - paned widget
 *
 * Description:
 *	Pops the top item off the pane stack.
 *
 * Returns: True if this is not the last element on the stack
 */
static Bool
PopPaneStack(PanedWidget pw)
{
    PaneStack *stack = pw->paned.stack;

    if (stack == NULL)
	return (False);

    pw->paned.stack = stack->next;
    XtFree((char *)stack);

    if (pw->paned.stack == NULL)
	return (False);

    return (True);
}

/*
 * Function:
 *	ClearPaneStack
 *
 * Parameters:
 *	pw - paned widget
 *
 * Description:
 *	Removes all entries from the pane stack.
 */
static void
ClearPaneStack(PanedWidget pw)
{
    while(PopPaneStack(pw))
	;
}

static void
XawPanedClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtAddConverter(XtRString, XtROrientation, XmuCvtStringToOrientation,
		   NULL, 0);
    XtSetTypeConverter(XtROrientation, XtRString, XmuCvtOrientationToString,
		       NULL, 0, XtCacheNone, NULL);
}

/* The Geometry Manager only allows changes after Realize if
 * allow_resize is True in the constraints record.
 *
 * For vertically paned widgets:
 *
 * It only allows height changes, but offers the requested height
 * as a compromise if both width and height changes were requested.
 *
 * For horizontal widgets the converse is true.
 * As all good Geometry Managers should, we will return No if the
 * request will have no effect; i.e. when the requestor is already
 * of the desired geometry.
 */
static XtGeometryResult
XawPanedGeometryManager(Widget w, XtWidgetGeometry *request,
			XtWidgetGeometry *reply)
{
    PanedWidget pw = (PanedWidget)XtParent(w);
    XtGeometryMask mask = request->request_mode;
    Dimension old_size, old_wpsize, old_paned_size;
    Pane pane = PaneInfo(w);
    Boolean vert = IsVert(pw);
    Dimension on_size, off_size;
    XtGeometryResult result;
    Boolean almost = False;

    /*
     * If any of the following is true, disallow the geometry change
     *
     * o The paned widget is realized and allow_resize is false for the pane
     * o The child did not ask to change the on_size
     * o The request is not a width or height request
     * o The requested size is the same as the current size
     */

    if ((XtIsRealized((Widget)pw) && !pane->allow_resize)
	|| !(mask & (vert ? CWHeight : CWWidth))
	|| (mask & (XtGeometryMask)(~(CWWidth | CWHeight)))
	|| GetRequestInfo(request, vert) ==  PaneSize(w, vert))
	return (XtGeometryNo);

    old_paned_size = PaneSize((Widget)pw, vert);
    old_wpsize = pane->wp_size;
    old_size = (Dimension)pane->size;

    pane->wp_size = (Dimension)(pane->size = GetRequestInfo(request, vert));

    AdjustPanedSize(pw, PaneSize((Widget)pw, !vert), &result, &on_size,
		    &off_size);

    /*
     * Fool the Refigure Locations proc to thinking that we are
     * a different on_size
     */

    if (result != XtGeometryNo) {
	if (vert)
	    XtHeight(pw) = on_size;
	else
	    XtWidth(pw) = on_size;
    }

    RefigureLocations(pw, PaneIndex(w), AnyPane);

    /*
     * Set up reply struct and reset core on_size
     */
    if (vert) {
	XtHeight(pw) = old_paned_size;
	reply->height = (Dimension) pane->size;
	reply->width = off_size;
    }
    else {
	XtWidth(pw) = old_paned_size;
	reply->height = off_size;
	reply->width = (Dimension) pane->size;
    }

    /*
     * IF either of the following is true
     *
     * o There was a "off_size" request and the new "off_size" is different
     *   from that requested
     * o There was no "off_size" request and the new "off_size" is different
     *
     * o The "on_size" we will allow is different from that requested
     *
     * THEN: set almost
     */
    if (!((vert ? CWWidth : CWHeight) & mask)) {
	if (vert)
	    request->width = XtWidth(w);
	else
	    request->height = XtHeight(w);
    }

    almost = GetRequestInfo(request, !vert) != GetRequestInfo(reply, !vert);
    almost = (Boolean)(almost | (GetRequestInfo(request, vert) != GetRequestInfo(reply, vert)));

    if ((mask & XtCWQueryOnly) || almost) {
	pane->wp_size = old_wpsize;
	pane->size = old_size;
	RefigureLocations(pw, PaneIndex(w), AnyPane);
	reply->request_mode = CWWidth | CWHeight;
	if (almost)
	    return (XtGeometryAlmost);
    }
    else {
	AdjustPanedSize(pw, PaneSize((Widget) pw, !vert), NULL, NULL, NULL);
	CommitNewLocations(pw);		/* layout already refigured */
    }

    return (XtGeometryDone);
}

/*ARGSUSED*/
static void
XawPanedInitialize(Widget request _X_UNUSED, Widget cnew,
		   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    PanedWidget pw = (PanedWidget)cnew;

    GetGCs((Widget)pw);

    pw->paned.recursively_called = False;
    pw->paned.stack = NULL;
    pw->paned.resize_children_to_pref = True;
    pw->paned.num_panes = 0;
}

static void
XawPanedRealize(Widget w, Mask *valueMask, XSetWindowAttributes *attributes)
{
    PanedWidget pw = (PanedWidget)w;
    Widget *childP;

    if ((attributes->cursor = pw->paned.cursor) != None)
	*valueMask |= CWCursor;

    (*SuperClass->core_class.realize)(w, valueMask, attributes);

    /*
     * Before we commit the new locations we need to realize all the panes and
     * their grips
     */
    ForAllPanes(pw, childP) {
	XtRealizeWidget(*childP);
	if (HasGrip(*childP))
	    XtRealizeWidget(PaneInfo(*childP)->grip);
    }

    RefigureLocationsAndCommit(w);
    pw->paned.resize_children_to_pref = False;
}

static void
XawPanedDestroy(Widget w)
{
    ReleaseGCs(w);
}

static void
ReleaseGCs(Widget w)
{
    PanedWidget pw = (PanedWidget)w;

    XtReleaseGC(w, pw->paned.normgc);
    XtReleaseGC(w, pw->paned.invgc);
    XtReleaseGC(w, pw->paned.flipgc);
}

static void
XawPanedInsertChild(Widget w)
{
    Pane pane = PaneInfo(w);

    /* insert the child widget in the composite children list with the
       superclass insert_child routine
     */
    (*SuperClass->composite_class.insert_child)(w);

    if (!IsPane(w))
	return;

    if (pane->show_grip == True) {
	CreateGrip(w);
	if (pane->min == PANED_GRIP_SIZE)
	    pane->min = PaneSize(pane->grip, IsVert((PanedWidget)XtParent(w)));
    }
    else {
	if (pane->min == PANED_GRIP_SIZE)
	    pane->min = 1;
	pane->grip = NULL;
    }

    pane->size = 0;
    pane->paned_adjusted_me = False;
}

static void
XawPanedDeleteChild(Widget w)
{
    /* remove the subwidget info and destroy the grip */
    if (IsPane(w) && HasGrip(w))
	XtDestroyWidget(PaneInfo(w)->grip);

    /* delete the child widget in the composite children list with the
       superclass delete_child routine
     */
    (*SuperClass->composite_class.delete_child)(w);
}

static void
XawPanedChangeManaged(Widget w)
{
    PanedWidget pw = (PanedWidget)w;
    Boolean vert = IsVert(pw);
    Dimension size;
    Widget *childP;

    if (pw->paned.recursively_called++)
	return;

    /*
     * If the size is zero then set it to the size of the widest or tallest pane
     */

    if ((size = PaneSize((Widget)pw, !vert)) == 0) {
	size = 1;
	ForAllChildren(pw, childP)
	if (XtIsManaged(*childP) && (PaneSize(*childP, !vert) > size))
	    size = PaneSize(*childP, !vert);
    }

    ManageAndUnmanageGrips(pw);
    pw->paned.recursively_called = False;
    ResortChildren(pw);

    pw->paned.num_panes = 0;
    ForAllChildren(pw, childP)
	if (IsPane(*childP)) {
	    if (XtIsManaged(*childP)) {
		Pane pane = PaneInfo(*childP);

		if (HasGrip(*childP))
		    PaneInfo(pane->grip)->position = pw->paned.num_panes;
		pane->position = pw->paned.num_panes; /* TEMPORARY -CDP 3/89 */
		pw->paned.num_panes++;
	    }
	    else
		break;		 /* This list is already sorted */
	}

    SetChildrenPrefSizes((PanedWidget) w, size);

    /*
     * ForAllPanes can now be used
     */
    if (PaneSize((Widget) pw, vert) == 0)
	AdjustPanedSize(pw, size, NULL, NULL, NULL);

    if (XtIsRealized((Widget)pw))
	RefigureLocationsAndCommit((Widget)pw);
}

static void
XawPanedResize(Widget w)
{
    SetChildrenPrefSizes((PanedWidget)w,
			 PaneSize(w, !IsVert((PanedWidget)w)));
    RefigureLocationsAndCommit(w);
}

/*ARGSUSED*/
static void
XawPanedRedisplay(Widget w, XEvent *event _X_UNUSED, Region region _X_UNUSED)
{
    DrawInternalBorders((PanedWidget)w);
}

/*ARGSUSED*/
static Boolean
XawPanedSetValues(Widget old, Widget request _X_UNUSED, Widget cnew,
		  ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    PanedWidget old_pw = (PanedWidget)old;
    PanedWidget new_pw = (PanedWidget)cnew;
    Boolean redisplay = False;

    if ((old_pw->paned.cursor != new_pw->paned.cursor) && XtIsRealized(cnew))
	XDefineCursor(XtDisplay(cnew), XtWindow(cnew), new_pw->paned.cursor);

    if (old_pw->paned.internal_bp != new_pw->paned.internal_bp ||
	old_pw->core.background_pixel != new_pw->core.background_pixel) {
	ReleaseGCs(old);
	GetGCs(cnew);
	redisplay = True;
    }

    if (old_pw->paned.grip_cursor != new_pw->paned.grip_cursor ||
	old_pw->paned.v_grip_cursor != new_pw->paned.v_grip_cursor ||
	old_pw->paned.h_grip_cursor != new_pw->paned.h_grip_cursor)
	ChangeAllGripCursors(new_pw);

    if (IsVert(old_pw) != IsVert(new_pw)) {
	/*
	 * We are fooling the paned widget into thinking that is needs to
	 * fully refigure everything, which is what we want
	 */
	if (IsVert(new_pw))
	    XtWidth(new_pw) = 0;
	else
	    XtHeight(new_pw) = 0;

	new_pw->paned.resize_children_to_pref = True;
	XawPanedChangeManaged(cnew); /* Seems weird, but does the right thing */
	new_pw->paned.resize_children_to_pref = False;
	if (new_pw->paned.grip_cursor == None)
	    ChangeAllGripCursors(new_pw);
	return (True);
    }

    if (old_pw->paned.internal_bw != new_pw->paned.internal_bw) {
	AdjustPanedSize(new_pw, PaneSize(cnew, !IsVert(old_pw)),
		        NULL, NULL, NULL);
	RefigureLocationsAndCommit(cnew);
	return (True);		/* We have done a full configuration, return */
    }

    if (old_pw->paned.grip_indent != new_pw->paned.grip_indent &&
	XtIsRealized(cnew)) {
	CommitNewLocations(new_pw);
	redisplay = True;
    }

    return (redisplay);
}

/*ARGSUSED*/
static Boolean
XawPanedPaneSetValues(Widget old, Widget request _X_UNUSED, Widget cnew,
		      ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    Pane old_pane = PaneInfo(old);
    Pane new_pane = PaneInfo(cnew);
    Boolean redisplay = False;

    /* Check for new min and max */
    if (old_pane->min != new_pane->min || old_pane->max != new_pane->max)
	XawPanedSetMinMax(cnew, (int)new_pane->min, (int)new_pane->max);

    /* Check for change in XtNshowGrip */
    if (old_pane->show_grip != new_pane->show_grip) {
	if (new_pane->show_grip == True) {
	    CreateGrip(cnew);
	    if (XtIsRealized(XtParent(cnew))) {
		if (XtIsManaged(cnew))	/* if paned is unrealized this will
					   happen automatically at realize time
					 */
		    XtManageChild(PaneInfo(cnew)->grip);	/* manage the grip */
		XtRealizeWidget(PaneInfo(cnew)->grip); /* realize the grip */
		CommitNewLocations((PanedWidget)XtParent(cnew));
	    }
	}
	else if (HasGrip(old)) {
	    XtDestroyWidget(old_pane->grip);
	    new_pane->grip = NULL;
	    redisplay = True;
	}
    }

    return (redisplay);
}

/*
 * Public routines
 */
/*
 * Function:
 *	XawPanedSetMinMax
 *
 * Parameters:
 *	widget - widget that is a child of the Paned widget
 *	min    - new min and max size for the pane
 *	max    - ""
 *
 * Description:
 *	Sets the min and max size for a pane.
 */
void
XawPanedSetMinMax(Widget widget, int min, int max)
{
    Pane pane = PaneInfo(widget);

    pane->min = (Dimension) min;
    pane->max = (Dimension) max;
    RefigureLocationsAndCommit(widget->core.parent);
}

/*
 * Function:
 *	XawPanedGetMinMax
 *
 * Parameters:
 *	widget - widget that is a child of the Paned widget
 *	min    - current min and max size for the pane (return)
 *	max    - ""
 *
 * Description:
 *	Gets the min and max size for a pane.
 */
void
XawPanedGetMinMax(Widget widget, int *min, int *max)
{
    Pane pane = PaneInfo(widget);

    *min = pane->min;
    *max = pane->max;
}

/*
 * Function:
 *	XawPanedSetRefigureMode
 *
 * Parameters:
 *	w    - paned widget
 *	mode - if False then inhibit refigure
 *
 * Description:
 *	Allows a flag to be set the will inhibit
 *		   the paned widgets relayout routine.
 */
void
XawPanedSetRefigureMode(Widget w,
#if NeedWidePrototypes
	int mode
#else
	Boolean mode
#endif
)
{
    ((PanedWidget)w)->paned.refiguremode = mode;
    RefigureLocationsAndCommit(w);
}

/*
 * Function:
 *	XawPanedGetNumSub
 *
 * Parameters:
 *	w - paned widget
 *
 * Description:
 *	Returns the number of panes in the paned widget.
 * Returns:
 *	the number of panes in the paned widget
 */
int
XawPanedGetNumSub(Widget w)
{
    return (((PanedWidget)w)->paned.num_panes);
}

/*
 * Function:
 *	XawPanedAllowResize
 *
 * Parameters:
 *	widget - child of the paned widget
 *
 * Description:
 *	  Allows a flag to be set that determines if the paned
 *	widget will allow geometry requests from this child.
 */
void
XawPanedAllowResize(Widget widget,
#if NeedWidePrototypes
	int allow_resize
#else
	Boolean allow_resize
#endif
)
{
    PaneInfo(widget)->allow_resize = allow_resize;
}
