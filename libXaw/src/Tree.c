/*

Copyright 1990, 1994, 1998  The Open Group

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

 * Copyright 1989 Prentice Hall
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose and without fee is hereby granted, provided that the above
 * copyright notice appear in all copies and that both the copyright notice
 * and this permission notice appear in supporting documentation.
 *
 * Prentice Hall and the authors disclaim all warranties with regard
 * to this software, including all implied warranties of merchantability and
 * fitness.  In no event shall Prentice Hall or the authors be liable
 * for any special, indirect or cosequential damages or any damages whatsoever
 * resulting from loss of use, data or profits, whether in an action of
 * contract, negligence or other tortious action, arising out of or in
 * connection with the use or performance of this software.
 *
 * Authors:  Jim Fulton, MIT X Consortium,
 *           based on a version by Douglas Young, Prentice Hall
 *
 * This widget is based on the Tree widget described on pages 397-419 of
 * Douglas Young's book "The X Window System, Programming and Applications
 * with Xt OSF/Motif Edition."  The layout code has been rewritten to use
 * additional blank space to make the structure of the graph easier to see
 * as well as to support vertical trees.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Cardinals.h>
#include <X11/Xaw/TreeP.h>
#include "Private.h"

#define IsHorizontal(tw) ((tw)->tree.gravity == WestGravity || \
			  (tw)->tree.gravity == EastGravity)

/*
 * Class Methods
 */
static void XawTreeChangeManaged(Widget);
static void XawTreeClassInitialize(void);
static void XawTreeConstraintDestroy(Widget);
static void XawTreeConstraintInitialize(Widget, Widget, ArgList, Cardinal*);
static Boolean XawTreeConstraintSetValues(Widget, Widget, Widget,
					  ArgList, Cardinal*);
static void XawTreeDestroy(Widget);
static XtGeometryResult XawTreeGeometryManager(Widget, XtWidgetGeometry*,
					       XtWidgetGeometry*);
static void XawTreeInitialize(Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult XawTreeQueryGeometry(Widget, XtWidgetGeometry*,
					     XtWidgetGeometry*);
static void XawTreeRedisplay(Widget, XEvent*, Region);
static Boolean XawTreeSetValues(Widget, Widget, Widget, ArgList, Cardinal*);

/*
 * Prototypes
 */
static void arrange_subtree(TreeWidget, Widget, int, int, int);
static void check_gravity(TreeWidget, XtGravity);
static void compute_bounding_box_subtree(TreeWidget, Widget, int);
static void delete_node(Widget, Widget);
static GC get_tree_gc(TreeWidget);
static void initialize_dimensions(Dimension**, int*, int);
static void insert_node(Widget, Widget);
static void layout_tree(TreeWidget, Bool);
static void set_positions(TreeWidget, Widget, int);
static void set_tree_size(TreeWidget, Bool, unsigned int, unsigned int);

/*
 * Initialization
 */

/*
 * resources of the tree itself
 */
static XtResource resources[] = {
    { XtNautoReconfigure, XtCAutoReconfigure, XtRBoolean, sizeof (Boolean),
	XtOffsetOf(TreeRec, tree.auto_reconfigure), XtRImmediate,
	(XtPointer) FALSE },
    { XtNhSpace, XtCHSpace, XtRDimension, sizeof (Dimension),
	XtOffsetOf(TreeRec, tree.hpad), XtRImmediate, (XtPointer) 0 },
    { XtNvSpace, XtCVSpace, XtRDimension, sizeof (Dimension),
	XtOffsetOf(TreeRec, tree.vpad), XtRImmediate, (XtPointer) 0 },
    { XtNforeground, XtCForeground, XtRPixel, sizeof (Pixel),
	XtOffsetOf(TreeRec, tree.foreground), XtRString,
	(XtPointer) XtDefaultForeground},
    { XtNlineWidth, XtCLineWidth, XtRDimension, sizeof (Dimension),
	XtOffsetOf(TreeRec, tree.line_width), XtRImmediate, (XtPointer) 0 },
    { XtNgravity, XtCGravity, XtRGravity, sizeof (XtGravity),
	XtOffsetOf(TreeRec, tree.gravity), XtRImmediate,
	(XtPointer) WestGravity },
#ifndef OLDXAW
    { XawNdisplayList, XawCDisplayList, XawRDisplayList, sizeof(XawDisplayList*),
	XtOffsetOf(TreeRec, tree.display_list), XtRImmediate,
	NULL },
#endif
};


/*
 * resources that are attached to all children of the tree
 */
static XtResource treeConstraintResources[] = {
    { XtNtreeParent, XtCTreeParent, XtRWidget, sizeof (Widget),
	XtOffsetOf(TreeConstraintsRec, tree.parent), XtRImmediate, NULL },
    { XtNtreeGC, XtCTreeGC, XtRGC, sizeof(GC),
	XtOffsetOf(TreeConstraintsRec, tree.gc), XtRImmediate, NULL },
};


TreeClassRec treeClassRec = {
  {
					/* core_class fields  */
    (WidgetClass) &constraintClassRec,	/* superclass         */
    "Tree",				/* class_name         */
    sizeof(TreeRec),			/* widget_size        */
    XawTreeClassInitialize,		/* class_init         */
    NULL,				/* class_part_init    */
    FALSE,				/* class_inited       */
    XawTreeInitialize,			/* initialize         */
    NULL,				/* initialize_hook    */
    XtInheritRealize,			/* realize            */
    NULL,				/* actions            */
    0,					/* num_actions        */
    resources,				/* resources          */
    XtNumber(resources),		/* num_resources      */
    NULLQUARK,				/* xrm_class          */
    TRUE,				/* compress_motion    */
    TRUE,				/* compress_exposure  */
    TRUE,				/* compress_enterleave*/
    TRUE,				/* visible_interest   */
    XawTreeDestroy,			/* destroy            */
    NULL,				/* resize             */
    XawTreeRedisplay,			/* expose             */
    XawTreeSetValues,			/* set_values         */
    NULL,				/* set_values_hook    */
    XtInheritSetValuesAlmost,		/* set_values_almost  */
    NULL,				/* get_values_hook    */
    NULL,				/* accept_focus       */
    XtVersion,				/* version            */
    NULL,				/* callback_private   */
    NULL,				/* tm_table           */
    XawTreeQueryGeometry,		/* query_geometry     */
    NULL,				/* display_accelerator*/
    NULL,				/* extension          */
  },
  {
					/* composite_class fields */
    XawTreeGeometryManager,		/* geometry_manager    */
    XawTreeChangeManaged,		/* change_managed      */
    XtInheritInsertChild,		/* insert_child        */
    XtInheritDeleteChild,		/* delete_child        */
    NULL,				/* extension           */
  },
  {
					/* constraint_class fields */
   treeConstraintResources,		/* subresources        */
   XtNumber(treeConstraintResources),	/* subresource_count   */
   sizeof(TreeConstraintsRec),		/* constraint_size     */
   XawTreeConstraintInitialize,		/* initialize          */
   XawTreeConstraintDestroy,		/* destroy             */
   XawTreeConstraintSetValues,		/* set_values          */
   NULL,				/* extension           */
   },
  {
					/* Tree class fields */
    NULL,				/* ignore              */
  }
};

WidgetClass treeWidgetClass = (WidgetClass) &treeClassRec;


/*****************************************************************************
 *                                                                           *
 *			     tree utility routines                           *
 *                                                                           *
 *****************************************************************************/

static void
initialize_dimensions(Dimension **listp, int *sizep, int n)
{
    int i;
    Dimension *l;

    if (!*listp) {
	*listp = (Dimension *) XtCalloc ((unsigned int) n,
					 (unsigned int) sizeof(Dimension));
	*sizep = ((*listp) ? n : 0);
	return;
    }
    if (n > *sizep) {
	*listp = (Dimension *) XtRealloc((char *) *listp,
					 (Cardinal) ((size_t)n*sizeof(Dimension)));
	if (!*listp) {
	    *sizep = 0;
	    return;
	}
	for (i = *sizep, l = (*listp) + i; i < n; i++, l++) *l = 0;
	*sizep = n;
    }
    return;
}

static GC
get_tree_gc(TreeWidget w)
{
    XtGCMask valuemask = GCBackground | GCForeground;
    XGCValues values;

    values.background = w->core.background_pixel;
    values.foreground = w->tree.foreground;
    if (w->tree.line_width != 0) {
	valuemask |= GCLineWidth;
	values.line_width = w->tree.line_width;
    }

    return XtGetGC ((Widget) w, valuemask, &values);
}

static void
insert_node(Widget parent, Widget node)
{
    TreeConstraints pc;
    TreeConstraints nc = TREE_CONSTRAINT(node);
    int nindex;

    nc->tree.parent = parent;

    if (parent == NULL) return;

    /*
     * If there isn't more room in the children array,
     * allocate additional space.
     */
    pc = TREE_CONSTRAINT(parent);
    nindex = pc->tree.n_children;

    if (pc->tree.n_children == pc->tree.max_children) {
	pc->tree.max_children += (pc->tree.max_children / 2) + 2;
	pc->tree.children = (WidgetList) XtRealloc ((char *)pc->tree.children,
						    (Cardinal)
						    ((size_t)pc->tree.max_children *
						    sizeof(Widget)));
    }

    /*
     * Add the sub_node in the next available slot and
     * increment the counter.
     */
    pc->tree.children[nindex] = node;
    pc->tree.n_children++;
}

static void
delete_node(Widget parent, Widget node)
{
    TreeConstraints pc;
    int pos, i;

    /*
     * Make sure the parent exists.
     */
    if (!parent) return;

    pc = TREE_CONSTRAINT(parent);

    /*
     * Find the sub_node on its parent's list.
     */
    for (pos = 0; pos < pc->tree.n_children; pos++)
      if (pc->tree.children[pos] == node) break;

    if (pos == pc->tree.n_children) return;

    /*
     * Decrement the number of children
     */
    pc->tree.n_children--;

    /*
     * Fill in the gap left by the sub_node.
     * Zero the last slot for good luck.
     */
    for (i = pos; i < pc->tree.n_children; i++)
      pc->tree.children[i] = pc->tree.children[i+1];

    pc->tree.children[pc->tree.n_children] = NULL;
}

static void
check_gravity(TreeWidget tw, XtGravity grav)
{
    switch (tw->tree.gravity) {
      case WestGravity: case NorthGravity: case EastGravity: case SouthGravity:
	break;
      default:
	tw->tree.gravity = grav;
	break;
    }
}


/*****************************************************************************
 *                                                                           *
 *			      tree class methods                             *
 *                                                                           *
 *****************************************************************************/

static void
XawTreeClassInitialize(void)
{
    XawInitializeWidgetSet();
  XtAddConverter(XtRString, XtRGravity, XmuCvtStringToGravity, NULL, 0);
  XtSetTypeConverter(XtRGravity, XtRString, XmuCvtGravityToString,
		     NULL, 0, XtCacheNone, NULL);
}


/*ARGSUSED*/
static void
XawTreeInitialize(Widget grequest, Widget gnew,
		  ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TreeWidget request = (TreeWidget) grequest, cnew = (TreeWidget) gnew;
    Arg arglist[2];

    /*
     * Make sure the widget's width and height are
     * greater than zero.
     */
    if (request->core.width <= 0) cnew->core.width = 5;
    if (request->core.height <= 0) cnew->core.height = 5;

    /*
     * Set the padding according to the orientation
     */
    if (request->tree.hpad == 0 && request->tree.vpad == 0) {
	if (IsHorizontal (request)) {
	    cnew->tree.hpad = TREE_HORIZONTAL_DEFAULT_SPACING;
	    cnew->tree.vpad = TREE_VERTICAL_DEFAULT_SPACING;
	} else {
	    cnew->tree.hpad = TREE_VERTICAL_DEFAULT_SPACING;
	    cnew->tree.vpad = TREE_HORIZONTAL_DEFAULT_SPACING;
	}
    }

    /*
     * Create a graphics context for the connecting lines.
     */
    cnew->tree.gc = get_tree_gc (cnew);

    /*
     * Create the hidden root widget.
     */
    cnew->tree.tree_root = (Widget) NULL;
    XtSetArg(arglist[0], XtNwidth, 1);
    XtSetArg(arglist[1], XtNheight, 1);
    cnew->tree.tree_root = XtCreateWidget ("root", widgetClass, gnew,
					  arglist,TWO);

    /*
     * Allocate the array used to hold the widest values per depth
     */
    cnew->tree.largest = NULL;
    cnew->tree.n_largest = 0;
    initialize_dimensions (&cnew->tree.largest, &cnew->tree.n_largest,
			   TREE_INITIAL_DEPTH);

    /*
     * make sure that our gravity is one of the acceptable values
     */
    check_gravity (cnew, WestGravity);
}


/* ARGSUSED */
static void
XawTreeConstraintInitialize(Widget request _X_UNUSED, Widget cnew,
			    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TreeConstraints tc = TREE_CONSTRAINT(cnew);
    TreeWidget tw = (TreeWidget) cnew->core.parent;

    /*
     * Initialize the widget to have no sub-nodes.
     */
    tc->tree.n_children = 0;
    tc->tree.max_children = 0;
    tc->tree.children = (Widget *) NULL;
    tc->tree.x = tc->tree.y = 0;
    tc->tree.bbsubwidth = 0;
    tc->tree.bbsubheight = 0;


    /*
     * If this widget has a super-node, add it to that
     * widget' sub-nodes list. Otherwise make it a sub-node of
     * the tree_root widget.
     */
    if (tc->tree.parent)
      insert_node (tc->tree.parent, cnew);
    else if (tw->tree.tree_root)
      insert_node (tw->tree.tree_root, cnew);
}


/* ARGSUSED */
static Boolean
XawTreeSetValues(Widget gcurrent, Widget grequest _X_UNUSED, Widget gnew,
		 ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TreeWidget current = (TreeWidget) gcurrent, cnew = (TreeWidget) gnew;
    Boolean redraw = FALSE;

    /*
     * If the foreground color has changed, redo the GC's
     * and indicate a redraw.
     */
    if (cnew->tree.foreground != current->tree.foreground ||
	cnew->core.background_pixel != current->core.background_pixel ||
	cnew->tree.line_width != current->tree.line_width) {
	XtReleaseGC (gnew, cnew->tree.gc);
	cnew->tree.gc = get_tree_gc (cnew);
	redraw = TRUE;
    }

    /*
     * If the minimum spacing has changed, recalculate the
     * tree layout. layout_tree() does a redraw, so we don't
     * need SetValues to do another one.
     */
    if (cnew->tree.gravity != current->tree.gravity) {
	check_gravity (cnew, current->tree.gravity);
    }

    if (IsHorizontal(cnew) != IsHorizontal(current)) {
	if (cnew->tree.vpad == current->tree.vpad &&
	    cnew->tree.hpad == current->tree.hpad) {
	    cnew->tree.vpad = current->tree.hpad;
	    cnew->tree.hpad = current->tree.vpad;
	}
    }

    if (cnew->tree.vpad != current->tree.vpad ||
	cnew->tree.hpad != current->tree.hpad ||
	cnew->tree.gravity != current->tree.gravity) {
	layout_tree (cnew, TRUE);
	redraw = FALSE;
    }
    return redraw;
}


/* ARGSUSED */
static Boolean
XawTreeConstraintSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
			   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    TreeConstraints newc = TREE_CONSTRAINT(cnew);
    TreeConstraints curc = TREE_CONSTRAINT(current);
    TreeWidget tw = (TreeWidget) cnew->core.parent;

    /*
     * If the parent field has changed, remove the widget
     * from the old widget's children list and add it to the
     * new one.
     */
    if (curc->tree.parent != newc->tree.parent){
	if (curc->tree.parent)
	  delete_node (curc->tree.parent, cnew);
	if (newc->tree.parent)
	  insert_node(newc->tree.parent, cnew);

	/*
	 * If the Tree widget has been realized,
	 * compute new layout.
	 */
	if (XtIsRealized((Widget)tw))
	  layout_tree (tw, FALSE);
    }
    return False;
}


static void
XawTreeConstraintDestroy(Widget w)
{
    TreeConstraints tc = TREE_CONSTRAINT(w);
    TreeWidget tw = (TreeWidget) XtParent(w);
    int i;

    /*
     * Remove the widget from its parent's sub-nodes list and
     * make all this widget's sub-nodes sub-nodes of the parent.
     */

    if (tw->tree.tree_root == w) {
	if (tc->tree.n_children > 0)
	  tw->tree.tree_root = tc->tree.children[0];
	else
	  tw->tree.tree_root = NULL;
    }

    delete_node (tc->tree.parent, (Widget) w);
    for (i = 0; i< tc->tree.n_children; i++)
      insert_node (tc->tree.parent, tc->tree.children[i]);

    layout_tree ((TreeWidget) (w->core.parent), FALSE);
}

/* ARGSUSED */
static XtGeometryResult
XawTreeGeometryManager(Widget w, XtWidgetGeometry *request,
		       XtWidgetGeometry *reply _X_UNUSED)
{

    TreeWidget tw = (TreeWidget) w->core.parent;

    /*
     * No position changes allowed!.
     */
    if ((request->request_mode & CWX && request->x!=w->core.x)
	||(request->request_mode & CWY && request->y!=w->core.y))
      return (XtGeometryNo);

    /*
     * Allow all resize requests.
     */

    if (request->request_mode & CWWidth)
      w->core.width = request->width;
    if (request->request_mode & CWHeight)
      w->core.height = request->height;
    if (request->request_mode & CWBorderWidth)
      w->core.border_width = request->border_width;

    if (tw->tree.auto_reconfigure) layout_tree (tw, FALSE);
    return (XtGeometryYes);
}

static void
XawTreeChangeManaged(Widget gw)
{
    layout_tree ((TreeWidget) gw, FALSE);
}


static void
XawTreeDestroy(Widget gw)
{
    TreeWidget w = (TreeWidget) gw;

    XtReleaseGC (gw, w->tree.gc);
    if (w->tree.largest) XtFree ((char *) w->tree.largest);
}


/* ARGSUSED */
static void
XawTreeRedisplay(Widget gw, XEvent *event _X_UNUSED, Region region _X_UNUSED)
{
    TreeWidget tw = (TreeWidget) gw;

#ifndef OLDXAW
    if (tw->tree.display_list)
	XawRunDisplayList(gw, tw->tree.display_list, event, region);
#endif

    /*
     * If the Tree widget is visible, visit each managed child.
     */
    if (tw->core.visible) {
	Cardinal i;
	int j;
	Display *dpy = XtDisplay (tw);
	Window w = XtWindow (tw);

	for (i = 0; i < tw->composite.num_children; i++) {
	    Widget child = tw->composite.children[i];
	    TreeConstraints tc = TREE_CONSTRAINT(child);

	    /*
	     * Don't draw lines from the fake tree_root.
	     */
	    if (child != tw->tree.tree_root && tc->tree.n_children) {
		int srcx = child->core.x + child->core.border_width;
		int srcy = child->core.y + child->core.border_width;

		switch (tw->tree.gravity) {
		  case WestGravity:
		    srcx += child->core.width + child->core.border_width;
		    /* fall through */
		  case EastGravity:
		    srcy += child->core.height / 2;
		    break;

		  case NorthGravity:
		    srcy += child->core.height + child->core.border_width;
		    /* fall through */
		  case SouthGravity:
		    srcx += child->core.width / 2;
		    break;
		}

		for (j = 0; j < tc->tree.n_children; j++) {
		    Widget k = tc->tree.children[j];
		    GC gc = (tc->tree.gc ? tc->tree.gc : tw->tree.gc);

		    switch (tw->tree.gravity) {
		      case WestGravity:
			/*
			 * right center to left center
			 */
			XDrawLine (dpy, w, gc, srcx, srcy,
				   (int) k->core.x,
				   (k->core.y + ((int) k->core.border_width) +
				    ((int) k->core.height) / 2));
			break;

		      case NorthGravity:
			/*
			 * bottom center to top center
			 */
			XDrawLine (dpy, w, gc, srcx, srcy,
				   (k->core.x + ((int) k->core.border_width) +
				    ((int) k->core.width) / 2),
				   (int) k->core.y);
			break;

		      case EastGravity:
			/*
			 * left center to right center
			 */
			XDrawLine (dpy, w, gc, srcx, srcy,
				   (k->core.x +
				    (((int) k->core.border_width) << 1) +
				    (int) k->core.width),
				   (k->core.y + ((int) k->core.border_width) +
				    ((int) k->core.height) / 2));
			break;

		      case SouthGravity:
			/*
			 * top center to bottom center
			 */
			XDrawLine (dpy, w, gc, srcx, srcy,
				   (k->core.x + ((int) k->core.border_width) +
				    ((int) k->core.width) / 2),
				   (k->core.y +
				    (((int) k->core.border_width) << 1) +
				    (int) k->core.height));
			break;
		    }
		}
	    }
	}
    }
}

static XtGeometryResult
XawTreeQueryGeometry(Widget w, XtWidgetGeometry *intended,
		     XtWidgetGeometry *preferred)
{
    TreeWidget tw = (TreeWidget) w;

    preferred->request_mode = (CWWidth | CWHeight);
    preferred->width = tw->tree.maxwidth;
    preferred->height = tw->tree.maxheight;

    if (((intended->request_mode & (CWWidth | CWHeight)) ==
	 (CWWidth | CWHeight)) &&
	intended->width == preferred->width &&
	intended->height == preferred->height)
      return XtGeometryYes;
    else if (preferred->width == w->core.width &&
             preferred->height == w->core.height)
      return XtGeometryNo;
    else
      return XtGeometryAlmost;
}


/*****************************************************************************
 *                                                                           *
 *			     tree layout algorithm                           *
 *                                                                           *
 * Each node in the tree is "shrink-wrapped" with a minimal bounding         *
 * rectangle, laid next to its siblings (with a small about of padding in    *
 * between) and then wrapped with their parent.  Parents are centered about  *
 * their children (or vice versa if the parent is larger than the children). *
 *                                                                           *
 *****************************************************************************/

static void
compute_bounding_box_subtree(TreeWidget tree, Widget w, int depth)
{
    TreeConstraints tc = TREE_CONSTRAINT(w);  /* info attached to all kids */
    int i;
    Bool horiz = IsHorizontal (tree);
    Dimension newwidth, newheight;
    Dimension bw2 = (Dimension)(w->core.border_width * 2);

    /*
     * Set the max-size per level.
     */
    if (depth >= tree->tree.n_largest) {
	initialize_dimensions (&tree->tree.largest,
			       &tree->tree.n_largest, depth + 1);
    }
    newwidth = (Dimension)((horiz ? w->core.width : w->core.height) + bw2);
    if (tree->tree.largest[depth] < newwidth)
      tree->tree.largest[depth] = newwidth;


    /*
     * initialize
     */
    tc->tree.bbwidth = (Dimension)(w->core.width + bw2);
    tc->tree.bbheight = (Dimension)(w->core.height + bw2);

    if (tc->tree.n_children == 0) return;

    /*
     * Figure the size of the opposite dimension (vertical if tree is
     * horizontal, else vice versa).  The other dimension will be set
     * in the second pass once we know the maximum dimensions.
     */
    newwidth = 0;
    newheight = 0;
    for (i = 0; i < tc->tree.n_children; i++) {
	Widget child = tc->tree.children[i];
	TreeConstraints cc = TREE_CONSTRAINT(child);

	compute_bounding_box_subtree (tree, child, depth + 1);

	if (horiz) {
	    if (newwidth < cc->tree.bbwidth) newwidth = cc->tree.bbwidth;
	    newheight = (Dimension)(newheight + (tree->tree.vpad + cc->tree.bbheight));
	} else {
	    if (newheight < cc->tree.bbheight) newheight = cc->tree.bbheight;
	    newwidth = (Dimension)(newwidth + (tree->tree.hpad + cc->tree.bbwidth));
	}
    }


    tc->tree.bbsubwidth = newwidth;
    tc->tree.bbsubheight = newheight;

    /*
     * Now fit parent onto side (or top) of bounding box and correct for
     * extra padding.  Be careful of unsigned arithmetic.
     */
    if (horiz) {
	tc->tree.bbwidth = (Dimension)(tc->tree.bbwidth + (tree->tree.hpad + newwidth));
	newheight = (Dimension)(newheight - tree->tree.vpad);
	if (newheight > tc->tree.bbheight) tc->tree.bbheight = newheight;
    } else {
	tc->tree.bbheight = (Dimension)(tc->tree.bbheight + (tree->tree.vpad + newheight));
	newwidth = (Dimension)(newwidth - tree->tree.hpad);
	if (newwidth > tc->tree.bbwidth) tc->tree.bbwidth = newwidth;
    }
}


static void
set_positions(TreeWidget tw, Widget w, int level)
{
    int i;

    if (w) {
	TreeConstraints tc = TREE_CONSTRAINT(w);

	if (level > 0) {
	    /*
	     * mirror if necessary
	     */
	    switch (tw->tree.gravity) {
	      case EastGravity:
		tc->tree.x = (Position) (tw->tree.maxwidth -
			      w->core.width - tc->tree.x);
		break;

	      case SouthGravity:
		tc->tree.y = (Position) (tw->tree.maxheight -
			      w->core.height - tc->tree.y);
		break;
	    }

	    /*
	     * Move the widget into position.
	     */
	    XtMoveWidget (w, tc->tree.x, tc->tree.y);
	}

	/*
	 * Set the positions of all children.
	 */
	for (i = 0; i < tc->tree.n_children; i++)
	  set_positions (tw, tc->tree.children[i], level + 1);
    }
}


static void
arrange_subtree(TreeWidget tree, Widget w, int depth, int x, int y)
{
    TreeConstraints tc = TREE_CONSTRAINT(w);  /* info attached to all kids */
    TreeConstraints firstcc, lastcc;
    int i;
    int newx, newy;
    Bool horiz = IsHorizontal (tree);
    Widget child = NULL;
    Dimension tmp;
    Dimension bw2 = (Dimension)(w->core.border_width * 2);
    Bool relayout = True;


    /*
     * If no children, then just lay out where requested.
     */
    tc->tree.x = (Position)x;
    tc->tree.y = (Position)y;

    if (horiz) {
	int myh = (w->core.height + bw2);

	if (myh > (int)tc->tree.bbsubheight) {
	    y += (myh - (int)tc->tree.bbsubheight) / 2;
	    relayout = False;
	}
    } else {
	int myw = (w->core.width + bw2);

	if (myw > (int)tc->tree.bbsubwidth) {
	    x += (myw - (int)tc->tree.bbsubwidth) / 2;
	    relayout = False;
	}
    }

    if ((tmp = (Dimension)(x + tc->tree.bbwidth)) > tree->tree.maxwidth)
      tree->tree.maxwidth = tmp;
    if ((tmp = (Dimension)(y + tc->tree.bbheight)) > tree->tree.maxheight)
      tree->tree.maxheight = tmp;

    if (tc->tree.n_children == 0) return;


    /*
     * Have children, so walk down tree laying out children, then laying
     * out parents.
     */
    if (horiz) {
	newx = x + tree->tree.largest[depth];
	if (depth > 0) newx += tree->tree.hpad;
	newy = y;
    } else {
	newx = x;
	newy = y + tree->tree.largest[depth];
	if (depth > 0) newy += tree->tree.vpad;
    }

    for (i = 0; i < tc->tree.n_children; i++) {
	TreeConstraints cc;

	child = tc->tree.children[i];	/* last value is used outside loop */
	cc = TREE_CONSTRAINT(child);

	arrange_subtree (tree, child, depth + 1, newx, newy);
	if (horiz) {
	    newy += tree->tree.vpad + cc->tree.bbheight;
	} else {
	    newx += tree->tree.hpad + cc->tree.bbwidth;
	}
    }

    /*
     * now layout parent between first and last children
     */
    if (relayout) {
	Position adjusted;
	firstcc = TREE_CONSTRAINT (tc->tree.children[0]);
	lastcc = TREE_CONSTRAINT (child);

	/* Adjustments are disallowed if they result in a position above
         * or to the left of the originally requested position, because
	 * this could collide with the position of the previous sibling.
	 */
	if (horiz) {
	    tc->tree.x = (Position)x;
	    adjusted = (Position)(firstcc->tree.y +
	      ((lastcc->tree.y + (Position) child->core.height +
		(Position) child->core.border_width * 2 -
		firstcc->tree.y - (Position) w->core.height -
		(Position) w->core.border_width * 2 + 1) / 2));
	    if (adjusted > tc->tree.y) tc->tree.y = adjusted;
	} else {
	    adjusted = (Position)(firstcc->tree.x +
	      ((lastcc->tree.x + (Position) child->core.width +
		(Position) child->core.border_width * 2 -
		firstcc->tree.x - (Position) w->core.width -
		(Position) w->core.border_width * 2 + 1) / 2));
	    if (adjusted > tc->tree.x) tc->tree.x = adjusted;
	    tc->tree.y = (Position)y;
	}
    }
}

static void
set_tree_size(TreeWidget tw, Bool insetvalues,
	      unsigned int width, unsigned int height)
{
    if (insetvalues) {
	tw->core.width = (Dimension)width;
	tw->core.height = (Dimension)height;
    } else {
	Dimension replyWidth = 0, replyHeight = 0;
	XtGeometryResult result = XtMakeResizeRequest ((Widget) tw,
						       (Dimension)width,
						       (Dimension)height,
						       &replyWidth,
						       &replyHeight);
	/*
	 * Accept any compromise.
	 */
	if (result == XtGeometryAlmost)
	  XtMakeResizeRequest ((Widget) tw, replyWidth, replyHeight,
			       (Dimension *) NULL, (Dimension *) NULL);
    }
    return;
}

static void
layout_tree(TreeWidget tw, Bool insetvalues)
{
    int i;
    Dimension *dp;

    /*
     * Do a depth-first search computing the width and height of the bounding
     * box for the tree at that position (and below).  Then, walk again using
     * this information to layout the children at each level.
     */

    if (tw->tree.tree_root == NULL)
	return;

    tw->tree.maxwidth = tw->tree.maxheight = 0;
    for (i = 0, dp = tw->tree.largest; i < tw->tree.n_largest; i++, dp++)
      *dp = 0;
    initialize_dimensions (&tw->tree.largest, &tw->tree.n_largest,
			   tw->tree.n_largest);
    compute_bounding_box_subtree (tw, tw->tree.tree_root, 0);

   /*
    * Second pass to do final layout.  Each child's bounding box is stacked
    * on top of (if horizontal, else next to) on top of its siblings.  The
    * parent is centered between the first and last children.
    */
    arrange_subtree (tw, tw->tree.tree_root, 0, 0, 0);

    /*
     * Move each widget into place.
     */
    set_tree_size (tw, insetvalues, tw->tree.maxwidth, tw->tree.maxheight);
    set_positions (tw, tw->tree.tree_root, 0);

    /*
     * And redisplay.
     */
    if (XtIsRealized ((Widget) tw)) {
	XClearArea (XtDisplay(tw), XtWindow((Widget)tw), 0, 0, 0, 0, True);
    }
}



/*****************************************************************************
 *                                                                           *
 *				Public Routines                              *
 *                                                                           *
 *****************************************************************************/

void
XawTreeForceLayout(Widget tree)
{
    layout_tree ((TreeWidget) tree, FALSE);
}

