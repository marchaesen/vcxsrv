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
 * Author: Chris D. Peterson
 *         MIT X Consortium
 *         kit@expo.lcs.mit.edu
 *
 * Date:   January 12, 1989
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/ToggleP.h>
#include <X11/Xaw/XawInit.h>

/*
 * Class Methods
 */
static void XawToggleClassInitialize(void);
static void XawToggleInitialize(Widget, Widget, ArgList, Cardinal*);
static Boolean XawToggleSetValues(Widget, Widget, Widget, ArgList, Cardinal*);

/*
 * Prototypes
 */
static void AddToRadioGroup(RadioGroup*, Widget);
static void CreateRadioGroup(Widget, Widget);
static RadioGroup *GetRadioGroup(Widget);
static void RemoveFromRadioGroup(Widget);
static void TurnOffRadioSiblings(Widget);
static void XawToggleDestroy(Widget, XtPointer, XtPointer);

/*
 * Actions
 */
static void Notify(Widget, XEvent*, String*, Cardinal*);
static void Toggle(Widget, XEvent*, String*, Cardinal*);
static void ToggleSet(Widget, XEvent*, String*, Cardinal*);

/*
 * Initialization
 */
/*
 * The order of toggle and notify are important, as the state has
 * to be set when we call the notify proc
 */
static char defaultTranslations[] =
"<Enter>:"		"highlight(Always)\n"
"<Leave>:"		"unhighlight()\n"
"<Btn1Down>,<Btn1Up>:"	"toggle() notify()\n"
;

#define offset(field) XtOffsetOf(ToggleRec, field)
static XtResource resources[] = {
  {
    XtNstate,
    XtCState,
    XtRBoolean,
    sizeof(Boolean),
    offset(command.set),
    XtRString,
    (XtPointer)"off"
  },
  {
    XtNradioGroup,
    XtCWidget,
    XtRWidget,
    sizeof(Widget),
    offset(toggle.widget),
    XtRWidget,
    NULL
  },
  {
    XtNradioData,
    XtCRadioData,
    XtRPointer,
    sizeof(XtPointer),
    offset(toggle.radio_data),
    XtRPointer,
    NULL
  },
};
#undef offset

static XtActionsRec actionsList[] = {
  {"toggle",		Toggle},
  {"notify",		Notify},
  {"set",		ToggleSet},
};

#define Superclass	((CommandWidgetClass)&commandClassRec)
ToggleClassRec toggleClassRec = {
  /* core */
  {
    (WidgetClass)Superclass,		/* superclass		  */
    "Toggle",				/* class_name		  */
    sizeof(ToggleRec),			/* size			  */
    XawToggleClassInitialize,		/* class_initialize	  */
    NULL,				/* class_part_initialize  */
    False,				/* class_inited		  */
    XawToggleInitialize,		/* initialize		  */
    NULL,				/* initialize_hook	  */
    XtInheritRealize,			/* realize		  */
    actionsList,			/* actions		  */
    XtNumber(actionsList),		/* num_actions		  */
    resources,				/* resources		  */
    XtNumber(resources),		/* resource_count	  */
    NULLQUARK,				/* xrm_class		  */
    False,				/* compress_motion	  */
    True,				/* compress_exposure	  */
    True,				/* compress_enterleave	  */
    False,				/* visible_interest	  */
    NULL,				/* destroy		  */
    XtInheritResize,			/* resize		  */
    XtInheritExpose,			/* expose		  */
    XawToggleSetValues,			/* set_values		  */
    NULL,				/* set_values_hook	  */
    XtInheritSetValuesAlmost,		/* set_values_almost	  */
    NULL,				/* get_values_hook	  */
    NULL,				/* accept_focus		  */
    XtVersion,				/* version		  */
    NULL,				/* callback_private	  */
    defaultTranslations,		/* tm_table		  */
    XtInheritQueryGeometry,		/* query_geometry	  */
    XtInheritDisplayAccelerator,	/* display_accelerator	  */
    NULL,				/* extension		  */
  },
  /* simple */
  {
    XtInheritChangeSensitive,		/* change_sensitive */
  },
  /* label */
  {
    NULL,				/* extension */
  },
  /* command */
  {
    NULL,				/* extension */
  },
  /* toggle */
  {
    NULL,				/* Set */
    NULL,				/* Unset */
    NULL,				/* extension */
  }
};

WidgetClass toggleWidgetClass = (WidgetClass)&toggleClassRec;

/*
 * Implementation
 */
static void
XawToggleClassInitialize(void)
{
    XtActionList actions;
    Cardinal num_actions;
    Cardinal i;
    ToggleWidgetClass cclass = (ToggleWidgetClass)toggleWidgetClass;
    static XtConvertArgRec parentCvtArgs[] = {
	{XtBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.parent),
	 sizeof(Widget)}
    };

    XawInitializeWidgetSet();
    XtSetTypeConverter(XtRString, XtRWidget, XmuNewCvtStringToWidget,
		       parentCvtArgs, XtNumber(parentCvtArgs),
		       XtCacheNone, NULL);
    XtSetTypeConverter(XtRWidget, XtRString, XmuCvtWidgetToString,
		       NULL, 0, XtCacheNone, NULL);

    /*
     * Find the set and unset actions in the command widget's action table
     */
    XtGetActionList(commandWidgetClass, &actions, &num_actions);

    for (i = 0 ; i < num_actions ; i++) {
	if (streq(actions[i].string, "set"))
	    cclass->toggle_class.Set = actions[i].proc;
	if (streq(actions[i].string, "unset"))
	    cclass->toggle_class.Unset = actions[i].proc;

	if (cclass->toggle_class.Set != NULL &&
	    cclass->toggle_class.Unset != NULL)	{
	    XtFree((char *)actions);
	    return;
	}
    }

    /* We should never get here */
    XtError("Aborting, due to errors resolving bindings in the Toggle widget.");
}

/*ARGSUSED*/
static void
XawToggleInitialize(Widget request, Widget cnew,
		    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ToggleWidget tw = (ToggleWidget)cnew;
    ToggleWidget tw_req = (ToggleWidget)request;

    tw->toggle.radio_group = NULL;

    if (tw->toggle.radio_data == NULL)
	tw->toggle.radio_data = (XtPointer)cnew->core.name;

    if (tw->toggle.widget != NULL) {
	if (GetRadioGroup(tw->toggle.widget) == NULL)
	    CreateRadioGroup(cnew, tw->toggle.widget);
	else
	    AddToRadioGroup(GetRadioGroup(tw->toggle.widget), cnew);
    }
    XtAddCallback(cnew, XtNdestroyCallback, XawToggleDestroy, NULL);

    /*
     * Command widget assumes that the widget is unset, so we only
     * have to handle the case where it needs to be set
     *
     * If this widget is in a radio group then it may cause another
     * widget to be unset, thus calling the notify procedure
     *
     * I want to set the toggle if the user set the state to "On" in
     * the resource group, regardless of what my ancestors did
     */
    if (tw_req->command.set)
	ToggleSet(cnew, NULL, NULL, NULL);
}

/*ARGSUSED*/
static void
ToggleSet(Widget w, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    ToggleWidgetClass cclass = (ToggleWidgetClass)w->core.widget_class;

    TurnOffRadioSiblings(w);
    cclass->toggle_class.Set(w, event, NULL, NULL);
}

static void
Toggle(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    ToggleWidget tw = (ToggleWidget)w;
    ToggleWidgetClass cclass = (ToggleWidgetClass)w->core.widget_class;

    if (tw->command.set)
	cclass->toggle_class.Unset(w, event, NULL, NULL);
    else
	ToggleSet(w, event, params, num_params);
}

/*ARGSUSED*/
static void
Notify(Widget w, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    ToggleWidget tw = (ToggleWidget)w;
    long antilint = tw->command.set;

    XtCallCallbacks(w, XtNcallback, (XtPointer)antilint);
}

/*ARGSUSED*/
static Boolean
XawToggleSetValues(Widget current, Widget request, Widget cnew,
		   ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ToggleWidget oldtw = (ToggleWidget)current;
    ToggleWidget tw = (ToggleWidget)cnew;
    ToggleWidget rtw = (ToggleWidget)request;

    if (oldtw->toggle.widget != tw->toggle.widget)
	XawToggleChangeRadioGroup(cnew, tw->toggle.widget);

    if (!tw->core.sensitive && oldtw->core.sensitive && rtw->command.set)
	tw->command.set = True;

    if (oldtw->command.set != tw->command.set) {
	tw->command.set = oldtw->command.set;
	Toggle(cnew, NULL, NULL, NULL);
    }

    return (False);
}

/*
 * Function:
 *	XawToggleDestroy
 *
 * Parameters:
 *	w     - toggle widget that is being destroyed
 *	temp1 - not used
 *	temp2 - ""
 *
 * Description:
 *	Destroy Callback for toggle widget.
 */
/*ARGSUSED*/
static void
XawToggleDestroy(Widget w, XtPointer temp1 _X_UNUSED, XtPointer temp2 _X_UNUSED)
{
    RemoveFromRadioGroup(w);
}

/*
 * Function:
 *	GetRadioGroup
 *
 * Parameters:
 *	w - toggle widget who's radio group we are getting
 *
 * Description:
 *	Gets the radio group associated with a give toggle widget.
 *
 * Returns:
 *	The radio group associated with this toggle group
 */
static RadioGroup *
GetRadioGroup(Widget w)
{
    ToggleWidget tw = (ToggleWidget)w;

    if (tw == NULL)
	return (NULL);

    return (tw->toggle.radio_group);
}

/*
 * Function:
 *	CreateRadioGroup
 *
 * Parameters:
 *	w1 - toggle widgets to add to the radio group
 *	w2 - ""
 *
 * Description:
 *	Creates a radio group. give two widgets.
 *
 * Note:
 *	A pointer to the group is added to each widget's radio_group field.
 */
static void
CreateRadioGroup(Widget w1, Widget w2)
{
    ToggleWidget tw1 = (ToggleWidget)w1;
    ToggleWidget tw2 = (ToggleWidget) w2;

    if (tw1->toggle.radio_group != NULL || tw2->toggle.radio_group != NULL)
	XtAppWarning(XtWidgetToApplicationContext(w1),
		     "Toggle Widget Error - Attempting to create a "
		     "new toggle group, when one already exists.");

    AddToRadioGroup(NULL, w1);
    AddToRadioGroup(GetRadioGroup(w1), w2);
}

/*
 * Function:
 *	AddToRadioGroup
 *
 * Parameters:
 *	group - element of the radio group the we are adding to
 *	w     - new toggle widget to add to the group
 *
 * Description:
 *	Adds a toggle to the radio group.
 */
static void
AddToRadioGroup(RadioGroup *group, Widget w)
{
    ToggleWidget tw = (ToggleWidget)w;
    RadioGroup *local;

    local = (RadioGroup *)XtMalloc(sizeof(RadioGroup));
    local->widget = w;
    tw->toggle.radio_group = local;

    if (group == NULL) {		  /* Creating new group */
	group = local;
	group->next = NULL;
	group->prev = NULL;
	return;
    }
    local->prev = group;	  /* Adding to previous group */
    if ((local->next = group->next) != NULL)
	local->next->prev = local;
    group->next = local;
}

/*
 * Function:
 *	TurnOffRadioSiblings
 *
 * Parameters:
 *	widget - toggle widget
 *
 * Description:
 *	Deactivates all radio siblings.
 */
static void
TurnOffRadioSiblings(Widget w)
{
    RadioGroup *group;
    ToggleWidgetClass cclass = (ToggleWidgetClass)w->core.widget_class;

    if ((group = GetRadioGroup(w)) == NULL)	/* Punt if there is no group */
	return;

    /* Go to the top of the group */
    for (; group->prev != NULL ; group = group->prev)
	;

    while (group != NULL) {
	ToggleWidget local_tog = (ToggleWidget)group->widget;

	if (local_tog->command.set) {
	    cclass->toggle_class.Unset(group->widget, NULL, NULL, NULL);
	    Notify(group->widget, NULL, NULL, NULL);
	}
	group = group->next;
    }
}

/*
 * Function:
 *	RemoveFromRadioGroup
 *
 * Parameters:
 *	w - toggle widget to remove
 *
 * Description:
 *	Removes a toggle from a RadioGroup.
 */
static void
RemoveFromRadioGroup(Widget w)
{
    RadioGroup *group = GetRadioGroup(w);
    if (group != NULL) {
	if (group->prev != NULL)
	    (group->prev)->next = group->next;
	if (group->next != NULL)
	    (group->next)->prev = group->prev;
	XtFree((char *)group);
    }
}

/*
 * Function:
 *	XawToggleChangeRadioGroup
 *
 * Parameters:
 *	w	    - toggle widget to change groups
 *	radio_group - any widget in the new group
 *
 * Description:
 *	Allows a toggle widget to change radio groups.
 */
void
XawToggleChangeRadioGroup(Widget w, Widget radio_group)
{
    ToggleWidget tw = (ToggleWidget)w;
    RadioGroup *group;

    RemoveFromRadioGroup(w);

    /*
     * If the toggle that we are about to add is set then we will
     * unset all toggles in the new radio group
     */

    if (tw->command.set && radio_group != NULL)
	XawToggleUnsetCurrent(radio_group);

    if (radio_group != NULL) {
	if ((group = GetRadioGroup(radio_group)) == NULL)
	    CreateRadioGroup(w, radio_group);
	else
	    AddToRadioGroup(group, w);
    }
}

/*
 * Function:
 *	XawToggleGetCurrent
 *
 * Parameters:
 *	w - any toggle widget in the toggle group
 *
 * Description:
 *	  Returns the RadioData associated with the toggle
 *	widget that is currently active in a toggle group.
 *
 * Returns:
 *	The XtNradioData associated with the toggle widget
 */
XtPointer
XawToggleGetCurrent(Widget w)
{
    RadioGroup *group;

    if ((group = GetRadioGroup(w)) == NULL)
	return (NULL);

    for (; group->prev != NULL ; group = group->prev)
	;

    while (group != NULL) {
	ToggleWidget local_tog = (ToggleWidget)group->widget;

	if (local_tog->command.set)
	    return (local_tog->toggle.radio_data);
	group = group->next;
    }

    return (NULL);
}

/*
 * Function:
 *	XawToggleSetCurrent
 *
 * Parameters:
 *	radio_group - any toggle widget in the toggle group
 *	radio_data  - radio data of the toggle widget to set
 *
 * Description:
 *	Sets the Toggle widget associated with the radio_data specified.
 */
void
XawToggleSetCurrent(Widget radio_group, XtPointer radio_data)
{
    RadioGroup *group;
    ToggleWidget local_tog;

    /* Special case of no radio group */

    if ((group = GetRadioGroup(radio_group)) == NULL) {
	local_tog = (ToggleWidget)radio_group;

	if (local_tog->toggle.radio_data == radio_data &&
	    !local_tog->command.set) {
	    ToggleSet(radio_group, NULL, NULL, NULL);
	    Notify(radio_group, NULL, NULL, NULL);
	}
	return;
    }

    /*
     * find top of radio_roup
     */
    for (; group->prev != NULL ; group = group->prev)
	;

    /*
     * search for matching radio data
     */
    while (group != NULL) {
	local_tog = (ToggleWidget)group->widget;

	if (local_tog->toggle.radio_data == radio_data) {
	    if (!local_tog->command.set) {	/* if not already set */
		ToggleSet(group->widget, NULL, NULL, NULL);
		Notify(group->widget, NULL, NULL, NULL);
	    }
	    return;			/* found it, done */
	}
	group = group->next;
    }
}

/*
 * Function:
 *	XawToggleUnsetCurrent
 *
 * Parameters:
 *	radio_group - any toggle widget in the toggle group
 *
 * Description:
 *	Unsets all Toggles in the radio_group specified.
 */
void
XawToggleUnsetCurrent(Widget radio_group)
{
    ToggleWidgetClass cclass;
    ToggleWidget local_tog = (ToggleWidget)radio_group;

    /* Special Case no radio group */

    if (local_tog->command.set) {
	cclass = (ToggleWidgetClass)local_tog->core.widget_class;
	cclass->toggle_class.Unset(radio_group, NULL, NULL, NULL);
	Notify(radio_group, NULL, NULL, NULL);
    }
    if (GetRadioGroup(radio_group) == NULL)
	return;

    TurnOffRadioSiblings(radio_group);
}
