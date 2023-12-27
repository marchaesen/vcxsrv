/*

Copyright 1989, 1998  The Open Group

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
 * Author:  Chris D. Peterson, Dave Sternlicht, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>	/* To get into the composite and core widget
				   structures. */
#include <X11/ObjectP.h>	/* For XtIs<Classname> macros. */
#include <X11/StringDefs.h>	/* for XtRString. */
#include <X11/ShellP.h>		/* for Application Shell Widget class. */

#include <X11/Xatom.h>
#include <X11/Xos.h>		/* for strcpy declaration */
#include <X11/Xfuncs.h>
#include <X11/Xmu/EditresP.h>
#include <X11/Xmd.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/SysUtil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _XEditResPutBool _XEditResPut8
#define _XEditResPutResourceType _XEditResPut8

/*
 * Types
 */
typedef enum {
    BlockNone,
    BlockSetValues,
    BlockAll
} EditresBlock;

typedef struct _SetValuesEvent {
    EditresCommand type;		/* first field must be type */
    WidgetInfo *widgets;
    unsigned short num_entries;		/* number of set values requests */
    char *name;
    char *res_type;
    XtPointer value;
    unsigned short value_len;
} SetValuesEvent;

typedef struct _SVErrorInfo {
    SetValuesEvent *event;
    ProtocolStream *stream;
    unsigned short *count;
    WidgetInfo *entry;
} SVErrorInfo;

typedef struct _GetValuesEvent {
    EditresCommand type;		/* first field must be type */
    WidgetInfo *widgets;
    unsigned short num_entries;		/* number of get values requests */
    char *name;
} GetValuesEvent;

typedef struct _FindChildEvent {
    EditresCommand type;		/* first field must be type */
    WidgetInfo *widgets;
    short x, y;
} FindChildEvent;

typedef struct _GenericGetEvent {
    EditresCommand type;		/* first field must be type */
    WidgetInfo *widgets;
    unsigned short num_entries;		/* number of set values requests */
} GenericGetEvent, GetResEvent, GetGeomEvent;

/*
 * Common to all events
 */
typedef struct _AnyEvent {
    EditresCommand type;		/* first field must be type */
    WidgetInfo *widgets;
} AnyEvent;

/*
 * The event union
 */
typedef union _EditresEvent {
    AnyEvent any_event;
    SetValuesEvent set_values_event;
    GetResEvent get_resources_event;
    GetGeomEvent get_geometry_event;
    FindChildEvent find_child_event;
} EditresEvent;

typedef struct _Globals {
    EditresBlock block;
    SVErrorInfo error_info;
    ProtocolStream stream;
    ProtocolStream *command_stream;	/* command stream */
#if defined(LONG64) || defined(WORD64)
    unsigned long base_address;
#endif
} Globals;

#define CURRENT_PROTOCOL_VERSION 5L

#define streq(a,b) (strcmp((a), (b)) == 0)

/*
 * Prototypes
 */
static Widget _FindChild(Widget, int, int);
static void _XEditresGetStringValues(Widget, Arg*, int);
static XtPointer BuildReturnPacket(ResIdent, EditResError, ProtocolStream*);
static void CommandDone(Widget, Atom*, Atom*);
static Boolean ConvertReturnCommand(Widget, Atom*, Atom*, Atom*, XtPointer*,
				    unsigned long*, int*);
static Boolean CvtStringToBlock(Display*, XrmValue*, Cardinal*,
				XrmValue*, XrmValue*, XtPointer*);
static EditresEvent *BuildEvent(Widget, Atom, XtPointer, ResIdent,
				unsigned long);
static _Xconst char *DoFindChild(Widget, EditresEvent*, ProtocolStream*);
static _Xconst char *DoGetGeometry(Widget, EditresEvent*, ProtocolStream*);
static _Xconst char *DoGetResources(Widget, EditresEvent*, ProtocolStream*);
static _Xconst char *DoSetValues(Widget, EditresEvent*, ProtocolStream*);
static void DumpChildren(Widget, ProtocolStream*, unsigned short*);
static _Xconst char *DumpValues(Widget, EditresEvent*, ProtocolStream*);
static _Xconst char *DumpWidgets(Widget, EditresEvent*, ProtocolStream*);
static void ExecuteCommand(Widget, Atom, ResIdent, EditresEvent*);
static void ExecuteGetGeometry(Widget, ProtocolStream*);
static void ExecuteGetResources(Widget w, ProtocolStream *stream);
static void ExecuteSetValues(Widget, SetValuesEvent*, WidgetInfo*,
			     ProtocolStream*, unsigned short*);
static void FreeEvent(EditresEvent*);
static void GetCommand(Widget w, XtPointer, Atom*, Atom*, XtPointer,
		       unsigned long*, int*);
static void HandleToolkitErrors(String, String, String, String,
				String*, Cardinal*);
static void InsertWidget(ProtocolStream*, Widget);
static Bool IsChild(Widget, Widget, Widget);
static Bool isApplicationShell(Widget);
static void LoadResources(Widget);
static Bool PositionInChild(Widget, int, int);
static int qcmp_widget_list(register _Xconst void*, register _Xconst void*);
static void SendCommand(Widget, Atom, ResIdent, EditResError,
			ProtocolStream*);
static void SendFailure(Widget, Atom, ResIdent, _Xconst char*);
static _Xconst char *VerifyWidget(Widget, WidgetInfo*);

/*
 * External
 */
void _XEditResCheckMessages(Widget, XtPointer, XEvent*, Boolean*);

/*
 * Initialization
 */
static Atom res_editor_command, res_editor_protocol, client_value;
static Globals globals;

/************************************************************
 * Resource Editor Communication Code
 ************************************************************/
/*
 * Function:
 *	_XEditResCheckMessages
 *
 * Parameters:
 *	data  - unused
 *	event - The X Event that triggered this handler
 *	cont  - unused
 *
 * Description:
 *	  This callback routine is set on all shell widgets, and checks to
 *	see if a client message event has come from the resource editor.
 */
/*ARGSUSED*/
void
_XEditResCheckMessages(Widget w, XtPointer data, XEvent *event, Boolean *cont)
{
    Time time;
    ResIdent ident;
    static Boolean first_time = False;
    static Atom res_editor, res_comm;
    Display *dpy;

    if (event->type == ClientMessage)
    {
	XClientMessageEvent * c_event = (XClientMessageEvent *)event;
	dpy = XtDisplay(w);

	if (!first_time)
	{
	    Atom atoms[4];
	    static _Xconst char *names[] = {
		EDITRES_NAME, EDITRES_COMMAND_ATOM,
		EDITRES_PROTOCOL_ATOM, EDITRES_CLIENT_VALUE
	    };

	    first_time = True;
	    XInternAtoms(dpy, (char **) names, 4, False, atoms);
	    res_editor = atoms[0];
	    res_editor_command = atoms[1];
	    res_editor_protocol = atoms[2];
	  /* Used in later procedures */
	    client_value = atoms[3];
	    LoadResources(w);
	}

	if ((c_event->message_type != res_editor)
	    || (c_event->format != EDITRES_SEND_EVENT_FORMAT))
	    return;

	time = c_event->data.l[0];
	res_comm = c_event->data.l[1];
	ident = (ResIdent) c_event->data.l[2];
	if (c_event->data.l[3] != CURRENT_PROTOCOL_VERSION)
	{
	    _XEditResResetStream(&globals.stream);
	    _XEditResPut8(&globals.stream,
			  (unsigned int) CURRENT_PROTOCOL_VERSION);
	    SendCommand(w, res_comm, ident, ProtocolMismatch, &globals.stream);
	    return;
	}

	XtGetSelectionValue(w, res_comm, res_editor_command,
			    GetCommand, (XtPointer)(long)ident, time);
    }
}

/*
 * Function:
 *	BuildEvent
 *
 * Parameters:
 *	w      - widget to own selection, in case of error
 *	sel    - selection to send error message beck in
 *	data   - the data for the request
 *	ident  - the id number we are looking for
 *	length - length of request
 *
 * Description:
 *	  Takes the info out the protocol stream an constructs
 *                   the proper event structure.
 *
 * Returns:
 *	the event, or NULL
 */
#if defined(ERROR_MESSAGE)
#undef ERROR_MESSAGE
#endif
#define ERROR_MESSAGE "Client: Improperly formatted protocol request"
static EditresEvent *
BuildEvent(Widget w, Atom sel, XtPointer data, ResIdent ident,
	   unsigned long length)
{
    EditresEvent *event;
    ProtocolStream alloc_stream, *stream;
    unsigned char temp;
    register unsigned int i;

    stream = &alloc_stream;
    stream->current = stream->top = (unsigned char *)data;
    stream->size = HEADER_SIZE;		/* size of header */

    /*
     * Retrieve the Header
     */
    if (length < HEADER_SIZE)
    {
	SendFailure(w, sel, ident, ERROR_MESSAGE);
	return (NULL);
    }

    (void)_XEditResGet8(stream, &temp);
    if (temp != ident)			/* Id's don't match, ignore request */
	return (NULL);

    event = (EditresEvent *)XtCalloc(sizeof(EditresEvent), 1);

    (void)_XEditResGet8(stream, &temp);
    event->any_event.type = (EditresCommand)temp;
    (void)_XEditResGet32(stream, &stream->size);
    stream->top = stream->current;	/* reset stream to top of value */

    /*
     * Now retrieve the data segment
     */
    switch(event->any_event.type)
    {
    case SendWidgetTree:
	break;			/* no additional info */
    case SetValues:
        {
	    SetValuesEvent *sv_event = (SetValuesEvent *)event;

	    if (!(_XEditResGetString8(stream, &sv_event->name)
		  && _XEditResGetString8(stream, &sv_event->res_type)))
		goto done;

	    /*
	     * Since we need the value length, we have to pull the
	     * value out by hand
	     */
	    if (!_XEditResGet16(stream, &sv_event->value_len))
		goto done;

	    sv_event->value = XtMalloc(sizeof(char) * (sv_event->value_len + 1));

	    for (i = 0; i < sv_event->value_len; i++)
		if (!_XEditResGet8(stream, (unsigned char *)sv_event->value + i))
		    goto done;

	    ((char*)sv_event->value)[i] = '\0';

	    if (!_XEditResGet16(stream, &sv_event->num_entries))
		goto done;

	    sv_event->widgets = (WidgetInfo *)
		XtCalloc(sizeof(WidgetInfo), sv_event->num_entries);

	    for (i = 0; i < sv_event->num_entries; i++)
		if (!_XEditResGetWidgetInfo(stream, sv_event->widgets + i))
		    goto done;
	}
	break;
    case FindChild:
	{
	    FindChildEvent *find_event = (FindChildEvent *)event;

	    find_event->widgets = (WidgetInfo *)XtCalloc(sizeof(WidgetInfo), 1);

	    if (!(_XEditResGetWidgetInfo(stream, find_event->widgets)
		  && _XEditResGetSigned16(stream, &find_event->x)
		  && _XEditResGetSigned16(stream, &find_event->y)))
		goto done;
	}
	break;
    case GetGeometry:
    case GetResources:
	{
	    GenericGetEvent *get_event = (GenericGetEvent *)event;

	    if (!_XEditResGet16(stream, &get_event->num_entries))
		goto done;

	    get_event->widgets = (WidgetInfo *)
		XtCalloc(sizeof(WidgetInfo), get_event->num_entries);

	    for (i = 0; i < get_event->num_entries; i++)
		if (!_XEditResGetWidgetInfo(stream, get_event->widgets + i))
		    goto done;
	}
	break;
    case GetValues:
	{
	    GetValuesEvent *gv_event = (GetValuesEvent *)event;

	    _XEditResGetString8(stream, &gv_event->name);
	    _XEditResGet16(stream, &gv_event->num_entries);
	    gv_event->widgets = (WidgetInfo *)
		XtCalloc(sizeof(WidgetInfo), gv_event->num_entries);
	    _XEditResGetWidgetInfo(stream, gv_event->widgets);
	}
        break;
    default:
	{
	    char buf[BUFSIZ];

	    XmuSnprintf(buf, sizeof(buf),
			"Unknown Protocol request %d.", event->any_event.type);
	    SendFailure(w, sel, ident, buf);
	    FreeEvent(event);
	    return (NULL);
	}
    }

    return (event);

  done:
    SendFailure(w, sel, ident, ERROR_MESSAGE);
    FreeEvent(event);
    return (NULL);
}

/*
 * Function:
 *	FreeEvent
 *
 * Parameters:
 *	event - event to free
 *
 * Description:
 *	Frees the event structure and any other pieces in it that need freeing.
 */
static void
FreeEvent(EditresEvent *event)
{
    if (event->any_event.widgets != NULL)
    {
	XtFree((char *)event->any_event.widgets->ids);
	XtFree((char *)event->any_event.widgets);
    }

    if (event->any_event.type == SetValues)
    {
	XtFree(event->set_values_event.name);
	XtFree(event->set_values_event.res_type);
    }

    XtFree((char *)event);
}

/*
 * Function:
 *	GetCommand
 *
 * Parameters:
 *	(See Xt XtConvertSelectionProc)
 *	data - contains the ident number for the command
 *
 * Description:
 *	Gets the Command out of the selection asserted by the resource manager.
 */
/*ARGSUSED*/
static void
GetCommand(Widget w, XtPointer data, Atom *selection, Atom *type,
	   XtPointer value, unsigned long *length, int *format)
{
    ResIdent ident = (ResIdent)(long)data;
    EditresEvent *event;

    if (*type != res_editor_protocol || *format != EDITRES_FORMAT)
	return;

    if ((event = BuildEvent(w, *selection, value, ident, *length)) != NULL)
    {
	ExecuteCommand(w, *selection, ident, event);
	FreeEvent(event);
    }
}

/*
 * Function:
 *	ExecuteCommand
 *
 * Parameters:
 *	w	- widget
 *	command	- the command to execute
 *	value	- the associated with the command
 *
 * Description:
 *	Executes a command string received from the resource editor.
 */
/*ARGSUSED*/
static void
ExecuteCommand(Widget w, Atom sel, ResIdent ident, EditresEvent *event)
{
    _Xconst char *(*func)(Widget, EditresEvent*, ProtocolStream*);
    _Xconst char *str;

    if (globals.block == BlockAll)
    {
	SendFailure(w, sel, ident,
		    "This client has blocked all Editres commands.");
	return;
    }
    else if (globals.block == BlockSetValues
	     && event->any_event.type == SetValues)
    {
	SendFailure(w, sel, ident,
		    "This client has blocked all SetValues requests.");
	return;
    }

    switch(event->any_event.type)
    {
    case SendWidgetTree:
#if defined(LONG64) || defined(WORD64)
	globals.base_address = (unsigned long)w & 0xFFFFFFFF00000000;
#endif
	func = DumpWidgets;
	break;
    case SetValues:
	func = DoSetValues;
	break;
    case FindChild:
	func = DoFindChild;
	break;
    case GetGeometry:
	func = DoGetGeometry;
	break;
    case GetResources:
	func = DoGetResources;
	break;
    case GetValues:
        func = DumpValues;
    break;
    default:
        {
	    char buf[BUFSIZ];

	    XmuSnprintf(buf, sizeof(buf),
			"Unknown Protocol request %d.",event->any_event.type);
	    SendFailure(w, sel, ident, buf);
	    return;
	}
    }

    _XEditResResetStream(&globals.stream);
    if ((str = (*func)(w, event, &globals.stream)) == NULL)
	SendCommand(w, sel, ident, PartialSuccess, &globals.stream);
    else
	SendFailure(w, sel, ident, str);
}

/*
 * Function:
 *	ConvertReturnCommand
 *
 * Parameters:
 *	w	   - the widget that owns the selection
 *	selection  - selection to convert
 *	target	   - target type for this selection
 *	type_ret   - type of the selection
 *	value_ret  - selection value
 *	length_ret - length of this selection
 *	format_ret - the format the selection is in
 *
 * Description:
 *	Converts a selection
 *
 * Returns:
 *	True if conversion was successful
 */
/*ARGSUSED*/
static Boolean
ConvertReturnCommand(Widget w, Atom *selection, Atom *target, Atom *type_ret,
		     XtPointer *value_ret, unsigned long *length_ret,
		     int *format_ret)
{
    /*
     * I assume the intrinsics give me the correct selection back
     */
    if ((*target != client_value))
	return (False);

    *type_ret = res_editor_protocol;
    *value_ret = (XtPointer)globals.command_stream->real_top;
    *length_ret = globals.command_stream->size + HEADER_SIZE;
    *format_ret = EDITRES_FORMAT;

  return (True);
}

/*
 * Function:
 *	CommandDone
 *
 * Parameters:
 *	widget	  - unused
 *	selection - unused
 *	target	  - unused
 *
 * Description:
 *	done with the selection
 */
/*ARGSUSED*/
static void
CommandDone(Widget widget, Atom *selection, Atom *target)
{
    /* Keep the toolkit from automatically freeing the selection value */
}

/*
 * Function:
 *	SendFailure
 *
 * Parameters:
 *	w     - widget to own the selection
 *	sel   - selection to assert
 *	ident - identifier
 *	str   - error message
 *
 * Description:
 *	Sends a failure message
 */
static void
SendFailure(Widget w, Atom sel, ResIdent ident, _Xconst char *str)
{
    _XEditResResetStream(&globals.stream);
    _XEditResPutString8(&globals.stream, str);
    SendCommand(w, sel, ident, Failure, &globals.stream);
}

/*
 * Function:
 *	BuildReturnPacket
 *
 * Parameters:
 *	ident   - identifier
 *	command - command code
 *	stream  - protocol stream
 * Description:
 *	Builds a return packet, given the data to send
 *
 * Returns:
 *	packet to send
 */
static XtPointer
BuildReturnPacket(ResIdent ident, EditResError error, ProtocolStream *stream)
{
    long old_alloc, old_size;
    unsigned char *old_current;

    /*
     * We have cleverly keep enough space at the top of the header
     * for the return protocol stream, so all we have to do is
     * fill in the space
     */
    /*
     * Fool the insert routines into putting the header in the right
     * place while being damn sure not to realloc (that would be very bad.)
     */
    old_current = stream->current;
    old_alloc = stream->alloc;
    old_size = stream->size;

    stream->current = stream->real_top;
    stream->alloc = stream->size + (2 * HEADER_SIZE);

    _XEditResPut8(stream, ident);
    _XEditResPut8(stream, (unsigned char)error);
    _XEditResPut32(stream, old_size);

    stream->alloc = old_alloc;
    stream->current = old_current;
    stream->size = old_size;

    return ((XtPointer)stream->real_top);
}

/*
 * Function:
 *	SendCommand
 * Parameters:
 *	w	- widget to own the selection
 *	sel	- selection to assert
 *	ident   - identifier
 *	command - command code
 *	stream  - protocol stream
 *
 * Description:
 *	Builds a return command line
 */
static void
SendCommand(Widget w, Atom sel, ResIdent ident, EditResError error,
	    ProtocolStream *stream)
{
    BuildReturnPacket(ident, error, stream);
    globals.command_stream = stream;

    /*
     * I REALLY want to own the selection.  Since this was not triggered
     * by a user action, and I am the only one using this atom it is safe to
     * use CurrentTime
     */
    XtOwnSelection(w, sel, CurrentTime, ConvertReturnCommand, NULL, CommandDone);
}

/************************************************************
 * Generic Utility Functions
 ************************************************************/
static int
qcmp_widget_list(register _Xconst void *left, register _Xconst void *right)
{
    return (char *)*(Widget **)left - (char *)*(Widget **)right;
}

/*
 * Function:
 *	FindChildren
 *
 * Parameters:
 *	parent   - parent widget
 *	children - list of children
 *	normal   - return normal children
 *	popup    - return popup children
 *	extra	 - return extra children
 *
 * Description:
 *	Returns all children (popup, normal and otherwise) of this widget
 *
 * Returns:
 *	number of children
 */
static int
FindChildren(Widget parent, Widget **children, Bool normal, Bool popup,
	     Bool extra)
{
    CompositeWidget cw = (CompositeWidget)parent;
    Cardinal i, num_children, current = 0;
    Widget *extra_widgets = NULL;
    Cardinal num_extra = 0;

    num_children = 0;

    if (XtIsWidget(parent) && popup)
	num_children += parent->core.num_popups;

    if (XtIsComposite(parent) && normal)
	num_children += cw->composite.num_children;

    if (XtIsWidget(parent) && extra)
    {
	XtResourceList norm_list, cons_list;
	Cardinal num_norm, num_cons;
	Arg args[1];
	Widget widget;

	XtGetResourceList(XtClass(parent), &norm_list, &num_norm);

	if (XtParent(parent) != NULL)
	    XtGetConstraintResourceList(XtClass(XtParent(parent)),
					&cons_list, &num_cons);
	else
	    num_cons = 0;

	extra_widgets = (Widget *)XtMalloc(sizeof(Widget));
	for (i = 0; i < num_norm; i++)
	    if (strcmp(norm_list[i].resource_type, XtRWidget) == 0)
	    {
		widget = NULL;
		XtSetArg(args[0], norm_list[i].resource_name, &widget);
		XtGetValues(parent, args, 1);
		if (widget && XtParent(widget) == parent)
		{
		    ++num_extra;
		    extra_widgets = (Widget *) XtRealloc(
			(char *)extra_widgets, num_extra * sizeof(Widget));
		    extra_widgets[num_extra - 1] = widget;
		}
	    }
	for (i = 0; i < num_cons; i++)
	    if (strcmp(cons_list[i].resource_type, XtRWidget) == 0)
	    {
		widget = NULL;
		XtSetArg(args[0], cons_list[i].resource_name, &widget);
		XtGetValues(parent, args, 1);
		if (widget && XtParent(widget) == parent)
		{
		    ++num_extra;
		    extra_widgets = (Widget *) XtRealloc(
			(char *)extra_widgets, num_extra * sizeof(Widget));
		    extra_widgets[num_extra - 1] = widget;
		}
	    }
	if (num_norm)
	    XtFree((char *)norm_list);
	if (num_cons)
	    XtFree((char *)cons_list);
    }

    if ((num_children + num_extra) == 0)
    {
	*children = NULL;
	return (0);
    }

    *children = (Widget *)XtMalloc(sizeof(Widget) * (num_children + num_extra));

    if (XtIsComposite(parent) && normal)
	for (i = 0; i < cw->composite.num_children; i++, current++)
	    (*children)[current] = cw->composite.children[i];

    if (XtIsWidget(parent) && popup)
	for (i = 0; i < parent->core.num_popups; i++, current++)
	    (*children)[current] = parent->core.popup_list[i];

    if (num_extra)
    /* Check for dups */
    {
	Cardinal j, old_num_extra = num_extra;

	qsort(extra_widgets, num_extra, sizeof(Widget), qcmp_widget_list);
	for (i = 0; i < num_extra - 1; i++)
	    while (i < num_extra - 1 &&
		   extra_widgets[i] == extra_widgets[i + 1])
	    {
		memmove(&extra_widgets[i], &extra_widgets[i + 1],
			(num_extra - i) * sizeof(Widget));
		--num_extra;
	    }

	for (i = 0; i < num_children; i++)
	    for (j = 0; j < num_extra; j++)
		if ((*children)[i] == extra_widgets[j])
		{
		    if ((j + 1) < num_extra)
			memmove(&extra_widgets[j], &extra_widgets[j + 1],
				(num_extra - j) * sizeof(Widget));
		    --num_extra;
		}

	if (old_num_extra != num_extra)
	    *children = (Widget *)XtRealloc((char *)*children, sizeof(Widget)
					    * (num_children + num_extra));

	if (num_extra)
	    memcpy(&(*children)[num_children], extra_widgets,
		   sizeof(Widget) * num_extra);
    }
    if (extra_widgets)
	XtFree((char *)extra_widgets);
    if (num_children + num_extra == 0)
    {
	XtFree((char *)*children);
	*children = NULL;
    }

    return (num_children + num_extra);
}

/*
 * Function:
 *	IsChild
 *
 * parameters:
 *	top    - top of the tree
 *	parent - parent widget
 *	child  - child widget
 *
 * Description:
 *	Check to see of child is a child of parent
 */
static Bool
IsChild(Widget top, Widget parent, Widget child)
{
    int i, num_children;
    Widget *children;

    if (parent == NULL)
	return (top == child);

    num_children = FindChildren(parent, &children, True, True, True);

    for (i = 0; i < num_children; i++)
	if (children[i] == child)
	{
	    XtFree((char *)children);
	    return (True);
	}

    XtFree((char *)children);
    return (False);
}

/*
 * Function:
 *	VerifyWidget
 *
 * Parameters:
 *	w    - any widget in the tree
 *	info - info about the widget to verify
 *
 * Description:
 *	Makes sure all the widgets still exist
 */
static _Xconst char *
VerifyWidget(Widget w, WidgetInfo *info)
{
    Widget top;
    register int count;
    register Widget parent;
    register unsigned long *child;

    for (top = w; XtParent(top) != NULL; top = XtParent(top))
	;

    parent = NULL;
    child = info->ids;
    count = 0;

    while (True)
    {
	if (!IsChild(top, parent, (Widget) *child))
	    return ("This widget no longer exists in the client.");

	if (++count == info->num_widgets)
	    break;

	parent = (Widget)*child++;
    }

    info->real_widget = (Widget)*child;

    return (NULL);
}

/************************************************************
 * Code to Perform SetValues operations
 ************************************************************/
/*
 * Function:
 *	DoSetValues
 *
 * Parameters:
 *	w      - a widget in the tree
 *	event  - event that caused this action
 *	stream - protocol stream to add
 *
 * Description:
 *	Performs the setvalues requested
 *
 * Returns:
 *	NULL
 */
static _Xconst char *
DoSetValues(Widget w, EditresEvent *event, ProtocolStream *stream)
{
    _Xconst char *str;
    register unsigned i;
    unsigned short count = 0;
    SetValuesEvent *sv_event = (SetValuesEvent *)event;

    _XEditResPut16(stream, count);  /* insert 0, will be overwritten later */

    for (i = 0; i < sv_event->num_entries; i++)
    {
	if ((str = VerifyWidget(w, &sv_event->widgets[i])) != NULL)
	{
	    _XEditResPutWidgetInfo(stream, &sv_event->widgets[i]);
	    _XEditResPutString8(stream, str);
	    count++;
	}
	else
	    ExecuteSetValues(sv_event->widgets[i].real_widget,
			     sv_event, sv_event->widgets + i, stream, &count);
    }

    /*
     * Overwrite the first 2 bytes with the real count.
     */
    *(stream->top) = count >> XER_NBBY;
    *(stream->top + 1) = count;

    return (NULL);
}

/*
 * Function:
 *	HandleToolkitErrors
 *
 * Parameters:
 *	name	   - name of the error
 *	type	   - type of the error
 *	class	   - class of the error
 *	msg	   - the default message
 *	params	   - the extra parameters for this message
 *	num_params - ""
 *
 *	Description: Handles X Toolkit Errors.
 */
/* ARGSUSED */
static void
HandleToolkitErrors(String name, String type, String class, String msg,
		    String *params, Cardinal *num_params)
{
    SVErrorInfo *info = &globals.error_info;
    char buf[BUFSIZ];

    if (streq(name, "unknownType"))
	XmuSnprintf(buf, sizeof(buf),
		    "The `%s' resource is not used by this widget.",
		    info->event->name);
    else if (streq(name, "noColormap"))
	XmuSnprintf(buf, sizeof(buf), msg, params[0]);
    else if (streq(name, "conversionFailed") || streq(name, "conversionError"))
    {
	if (streq((String)info->event->value, XtRString))
	    XmuSnprintf(buf, sizeof(buf),
			"Could not convert the string '%s' for the `%s' "
			"resource.", (String)info->event->value,
			info->event->name);
	else
	    XmuSnprintf(buf, sizeof(buf),
			"Could not convert the `%s' resource.",
			info->event->name);
    }
    else
	XmuSnprintf(buf, sizeof(buf),
		    "Name: %s, Type: %s, Class: %s, Msg: %s",
		    name, type, class, msg);

    /*
     * Insert this info into the protocol stream, and update the count
     */
    (*(info->count))++;
    _XEditResPutWidgetInfo(info->stream, info->entry);
    _XEditResPutString8(info->stream, buf);
}

/*
 * Function:
 *	ExecuteSetValues
 *
 * Parameters:
 *	w	 - widget to perform the set_values on
 *	sv_event - set values event
 *	sv_info  - set_value info
 *.
 * Description:
 *	Performs a setvalues for a given command
 */
static void
ExecuteSetValues(Widget w, SetValuesEvent *sv_event, WidgetInfo *entry,
		 ProtocolStream *stream, unsigned short *count)
{
    XtErrorMsgHandler old;
    SVErrorInfo *info = &globals.error_info;

    info->event = sv_event;	/* No data can be passed to */
    info->stream = stream;	/* an error handler, so we */
    info->count = count;	/* have to use a global */
    info->entry = entry;

    old = XtAppSetWarningMsgHandler(XtWidgetToApplicationContext(w),
				    HandleToolkitErrors);

    XtVaSetValues(w, XtVaTypedArg,
		  sv_event->name, sv_event->res_type,
		  sv_event->value, sv_event->value_len,
		  NULL);

    (void)XtAppSetWarningMsgHandler(XtWidgetToApplicationContext(w), old);
}

/************************************************************
 * Code for Creating and dumping widget tree.
 ************************************************************/
/* Function:
 *	DumpWidgets
 *
 * Parameters:
 *	w      - a widget in the tree
 *	event  - event that caused this action
 *	stream - protocol stream to add
 *
 * Description:
 *	  Given a widget it builds a protocol packet containing the entire
 *	widget hierarchy.
 *
 * Returns:
 *	NULL
 */
#define TOOLKIT_TYPE ("Xt")
/*ARGSUSED*/
static _Xconst char *
DumpWidgets(Widget w, EditresEvent *event, ProtocolStream *stream)
{
    unsigned short count = 0;

    /* Find Tree's root */
    for (; XtParent(w) != NULL; w = XtParent(w))
	;

    /*
     * hold space for count, overwritten later
     */
    _XEditResPut16(stream, (unsigned int)0);

    DumpChildren(w, stream, &count);

    /*
     * write out toolkit type
     */
    _XEditResPutString8(stream, TOOLKIT_TYPE);

    /*
     * Overwrite the first 2 bytes with the real count
     */
    *(stream->top) = count >> XER_NBBY;
    *(stream->top + 1) = count;

    return (NULL);
}

/*
 * Function:
 *	 DumpChildren
 *
 * Parameters:
 *	w      - widget to dump
 *	stream - stream to dump to
 *	count  - number of dumps we have performed
 *
 * Description:
 *	Adds a child's name to the list.
 */
/* This is a trick/kludge.  To make shared libraries happier (linking
 * against Xmu but not linking against Xt, and apparently even work
 * as we desire on SVR4, we need to avoid an explicit data reference
 * to applicationShellWidgetClass.  XtIsTopLevelShell is known
 * (implementation dependent assumption!) to use a bit flag.  So we
 * go that far.  Then, we test whether it is an applicationShellWidget
 * class by looking for an explicit class name.  Seems pretty safe.
 */
static Bool
isApplicationShell(Widget w)
{
    register WidgetClass c;

    if (!XtIsTopLevelShell(w))
	return (False);
    for (c = XtClass(w); c; c = c->core_class.superclass)
      if (strcmp(c->core_class.class_name, "ApplicationShell") == 0)
	  return (True);

    return (False);
}

static void
DumpChildren(Widget w, ProtocolStream *stream, unsigned short *count)
{
    int i, num_children;
    Widget *children;
    unsigned long window;
    String c_class;

    (*count)++;

    InsertWidget(stream, w);		/* Insert the widget into the stream */

    _XEditResPutString8(stream, XtName(w)); /* Insert name */

    if (isApplicationShell(w))
	c_class = ((ApplicationShellWidget)w)->application.class;
    else
	c_class = XtClass(w)->core_class.class_name;

    _XEditResPutString8(stream, c_class);	/* Insert class */

    if (XtIsWidget(w))
	if (XtIsRealized(w))
	    window = XtWindow(w);
	else
	    window = EDITRES_IS_UNREALIZED;
    else
	window = EDITRES_IS_OBJECT;

    _XEditResPut32(stream, window);		/* Insert window id */

    /*
     * Find children and recurse
     */
    num_children = FindChildren(w, &children, True, True, True);
    for (i = 0; i < num_children; i++)
	DumpChildren(children[i], stream, count);

    XtFree((char *)children);
}

/************************************************************
 * Code for getting the geometry of widgets
 ************************************************************/
/*
 * Function:
 *	DoGetGeometry
 *
 * Parameters:
 *	w      - widget in the tree
 *	event  - event that caused this action
 *	stream - protocol stream to add
 *
 * Description:
 *	Retrieves the Geometry of each specified widget.
 *
 * Returns:
 *	NULL
 */
static _Xconst char *
DoGetGeometry(Widget w, EditresEvent *event, ProtocolStream *stream)
{
    unsigned i;
    _Xconst char *str;
    GetGeomEvent *geom_event = (GetGeomEvent *)event;

    _XEditResPut16(stream, geom_event->num_entries);

    for (i = 0; i < geom_event->num_entries; i++)
    {
	/*
	 * Send out the widget id
	 */
	_XEditResPutWidgetInfo(stream, &geom_event->widgets[i]);

	if ((str = VerifyWidget(w, &geom_event->widgets[i])) != NULL)
	{
	    _XEditResPutBool(stream, True);	/* an error occurred */
	    _XEditResPutString8(stream, str);	/* set message */
	}
	else
	    ExecuteGetGeometry(geom_event->widgets[i].real_widget, stream);
    }

    return (NULL);
}

/*
 * Function:
 *	ExecuteGetGeometry
 *
 * Parameters:
 *	w      - widget to get geometry
 *	stream - stream to append to
 *
 * Description:
 *	Gets the geometry for each widget specified.
 *
 * Returns:
 *	True if no error occurred.
 */
static void
ExecuteGetGeometry(Widget w, ProtocolStream *stream)
{
    int i;
    Boolean mapped_when_man;
    Dimension width, height, border_width;
    Arg args[8];
    Cardinal num_args = 0;
    Position x, y;

    if (!XtIsRectObj(w) || (XtIsWidget(w) && !XtIsRealized(w)))
    {
	_XEditResPutBool(stream, False);	/* no error */
	_XEditResPutBool(stream, False);	/* not visible */
	for (i = 0; i < 5; i++)		/* fill in extra space with 0's */
	    _XEditResPut16(stream, 0);
	return;
    }

    XtSetArg(args[num_args], XtNwidth, &width); num_args++;
    XtSetArg(args[num_args], XtNheight, &height); num_args++;
    XtSetArg(args[num_args], XtNborderWidth, &border_width); num_args++;
    XtSetArg(args[num_args], XtNmappedWhenManaged, &mapped_when_man);
    num_args++;
    XtGetValues(w, args, num_args);

    if (!(XtIsManaged(w) && mapped_when_man) && XtIsWidget(w))
    {
	XWindowAttributes attrs;

	/*
	 * The toolkit does not maintain mapping state, we have
	 * to go to the server
	 */
	if (XGetWindowAttributes(XtDisplay(w), XtWindow(w), &attrs) != 0)
	{
	    if (attrs.map_state != IsViewable)
	    {
		_XEditResPutBool(stream, False);	/* no error */
		_XEditResPutBool(stream, False);	/* not visible */
		for (i = 0; i < 5; i++)	/* fill in extra space with 0's */
		    _XEditResPut16(stream, 0);
		return;
	    }
	}
	else
	{
	    _XEditResPut8(stream, True); /* Error occurred. */
	    _XEditResPutString8(stream, "XGetWindowAttributes failed.");
	    return;
	}
    }

    XtTranslateCoords(w, -((int) border_width), -((int) border_width), &x, &y);

    _XEditResPutBool(stream, False);	/* no error */
    _XEditResPutBool(stream, True);	/* Visible */
    _XEditResPut16(stream, x);
    _XEditResPut16(stream, y);
    _XEditResPut16(stream, width);
    _XEditResPut16(stream, height);
    _XEditResPut16(stream, border_width);
}

/************************************************************
 * Code for executing FindChild
 ************************************************************/
/*
 * Function:
 *	PositionInChild
 *
 * Parameters:
 *	child - child widget to check
 *	x     - location of point to check in the parent's coord space
 *	y     - ""
 *
 * Description:
 *	Returns true if this location is in the child.
 */
static Bool
PositionInChild(Widget child, int x, int y)
{
    Arg args[6];
    Cardinal num;
    Dimension width, height, border_width;
    Position child_x, child_y;
    Boolean mapped_when_managed;

    if (!XtIsRectObj(child))	/* we must at least be a rect obj */
	return (False);

    num = 0;
    XtSetArg(args[num], XtNmappedWhenManaged, &mapped_when_managed); num++;
    XtSetArg(args[num], XtNwidth, &width); num++;
    XtSetArg(args[num], XtNheight, &height); num++;
    XtSetArg(args[num], XtNx, &child_x); num++;
    XtSetArg(args[num], XtNy, &child_y); num++;
    XtSetArg(args[num], XtNborderWidth, &border_width); num++;
    XtGetValues(child, args, num);

    /*
     * The only way we will know of the widget is mapped is to see if
     * mapped when managed is True and this is a managed child.  Otherwise
     * we will have to ask the server if this window is mapped
     */
    if (XtIsWidget(child) && !(mapped_when_managed && XtIsManaged(child)))
    {
	XWindowAttributes attrs;

	if (XGetWindowAttributes(XtDisplay(child), XtWindow(child), &attrs)
	    &&  attrs.map_state != IsViewable)
	return (False);
    }

    return ((x >= child_x)
	    && (x <= (child_x + (Position)width + 2 * (Position)border_width))
	    && (y >= child_y)
	    && (y <= (child_y + (Position)height + 2 * (Position)border_width)));
}

/*
 * Function:
 *	_FindChild
 *
 * Parameters:
 *	parent - widget that is known to contain the point specified
 *	x      - point in coordinates relative to the widget specified
 *	y      - ""
 *
 * Description:
 *	Finds the child that actually contains the point shown.
 */
static Widget
_FindChild(Widget parent, int x, int y)
{
    Widget *children;
    int i = FindChildren(parent, &children, True, False, True);

    while (i > 0)
    {
	i--;

	if (PositionInChild(children[i], x, y))
	{
	    Widget child = children[i];

	    XtFree((char *)children);
	    return (_FindChild(child, x - child->core.x, y - child->core.y));
	}
    }

    XtFree((char *)children);

    return (parent);
}

/*
 * Function:
 *	DoFindChild
 *
 * Parameters:
 *	w      - widget in the tree
 *	event  - event that caused this action
 *	stream - protocol stream to add
 * Description:
 *	Finds the child that contains the location specified.
 *
 * Returns:
 *	  An allocated error message if something went horribly wrong and
 *	no set values were performed, else NULL.
 */
static _Xconst char *
DoFindChild(Widget w, EditresEvent *event, ProtocolStream *stream)
{
    _Xconst char *str;
    Widget parent, child;
    Position parent_x, parent_y;
    FindChildEvent *find_event = (FindChildEvent *)event;

    if ((str = VerifyWidget(w, find_event->widgets)) != NULL)
	return (str);

    parent = find_event->widgets->real_widget;

    XtTranslateCoords(parent, (Position) 0, (Position) 0,
		      &parent_x, &parent_y);

    child = _FindChild(parent, find_event->x - (int) parent_x,
		       find_event->y - (int) parent_y);

    InsertWidget(stream, child);

    return (NULL);
}

/************************************************************
 * Procedures for performing GetResources
 ************************************************************/
/*
 * Function:
 *	DoGetResources
 *
 * Parameters:
 *	w      - widget in the tree
 *	event  - event that caused this action
 *	stream - protocol stream to add
 *
 * Description:
 *	Gets the Resources associated with the widgets passed.
 *
 * Returns:
 *	NULL
 */
static _Xconst char *
DoGetResources(Widget w, EditresEvent *event, ProtocolStream *stream)
{
    unsigned int i;
    _Xconst char *str;
    GetResEvent *res_event = (GetResEvent *)event;

    _XEditResPut16(stream, res_event->num_entries); /* number of replies */

    for (i = 0; i < res_event->num_entries; i++)
    {
	/*
	 * Send out the widget id
	 */
	_XEditResPutWidgetInfo(stream, &res_event->widgets[i]);
	if ((str = VerifyWidget(w, &res_event->widgets[i])) != NULL)
	{
	    _XEditResPutBool(stream, True);	/* an error occurred */
	    _XEditResPutString8(stream, str);	/* set message */
	}
	else
	{
	    _XEditResPutBool(stream, False);	/* no error occurred */
	    ExecuteGetResources(res_event->widgets[i].real_widget, stream);
	}
    }

    return (NULL);
}

/* Function:
 *	ExecuteGetResources
 *
 * Parameters:
 *	w      - widget to get resources on
 *	stream - protocol stream
 *
 * Description:
 *	Gets the resources for any individual widget
 */
static void
ExecuteGetResources(Widget w, ProtocolStream *stream)
{
    XtResourceList norm_list, cons_list;
    Cardinal num_norm, num_cons;
    register Cardinal i;

    /*
     * Get Normal Resources
     */
    XtGetResourceList(XtClass(w), &norm_list, &num_norm);

    if (XtParent(w) != NULL)
	XtGetConstraintResourceList(XtClass(XtParent(w)), &cons_list,&num_cons);
    else
	num_cons = 0;

    _XEditResPut16(stream, num_norm + num_cons);	/* how many resources */

    /*
     * Insert all the normal resources
     */
    for (i = 0; i < num_norm; i++)
    {
	_XEditResPutResourceType(stream, NormalResource);
	_XEditResPutString8(stream, norm_list[i].resource_name);
	_XEditResPutString8(stream, norm_list[i].resource_class);
	_XEditResPutString8(stream, norm_list[i].resource_type);
    }
    XtFree((char *)norm_list);

    /*
     * Insert all the constraint resources
     */
    if (num_cons > 0)
    {
	for (i = 0; i < num_cons; i++)
	{
	    _XEditResPutResourceType(stream, ConstraintResource);
	    _XEditResPutString8(stream, cons_list[i].resource_name);
	    _XEditResPutString8(stream, cons_list[i].resource_class);
	    _XEditResPutString8(stream, cons_list[i].resource_type);
	}
	XtFree((char *)cons_list);
    }
}

/*
 * Function:
 *	DumpValues
 *
 * Parameters:
 *	event  - event that caused this action
 *	stream - protocol stream to add
 *
 * Description:
 *	Returns resource values to the resource editor.
 *
 * Returns:
 *	NULL
 */
/*ARGSUSED*/
static _Xconst char *
DumpValues(Widget w, EditresEvent* event, ProtocolStream* stream)
{
    _Xconst char *str;
    Arg warg[1];
    _Xconst _XtString res_value = NULL;
    GetValuesEvent *gv_event = (GetValuesEvent *)event;

    /* put the count in the stream */
    _XEditResPut16(stream, (unsigned int)1);

    /*
     * Get the resource of the widget asked for by the
     * resource editor and insert it into the stream
     */
    XtSetArg(warg[0], gv_event->name, &res_value);

    if ((str = VerifyWidget(w, &gv_event->widgets[0])) != NULL)
	_XEditResPutString8(stream, str);
    else
    {
	_XEditresGetStringValues(gv_event->widgets[0].real_widget, warg, 1);
	if (!res_value)
	    res_value = "NoValue";
	_XEditResPutString8(stream, res_value);
    }

    return (NULL);
}

/************************************************************
 * Code for inserting values into the protocol stream
 ************************************************************/
/*
 * Function:
 *	InsertWidget
 *
 * Parameters:
 *	stream - protocol stream
 *	w      - widget to insert
 *
 * Description:
 *	  Inserts the full parent hierarchy of this widget into the protocol
 *	stream as a widget list.
 */
static void
InsertWidget(ProtocolStream *stream, Widget w)
{
    Widget temp;
    unsigned long *widget_list;
    register int i, num_widgets;

    for (temp = w, i = 0; temp != NULL; temp = XtParent(temp), i++)
	;

    num_widgets = i;
    widget_list = (unsigned long *)XtMalloc(sizeof(unsigned long) * num_widgets);

    /*
     * Put the widgets into the list
     * make sure that they are inserted in the list from parent -> child
     */
    for (i--, temp = w; temp != NULL; temp = XtParent(temp), i--)
    widget_list[i] = (unsigned long)temp;

    _XEditResPut16(stream, num_widgets);	/* insert number of widgets */
    for (i = 0; i < num_widgets; i++)		/* insert Widgets themselves */
	_XEditResPut32(stream, widget_list[i]);

    XtFree((char *)widget_list);
}

/************************************************************
 * All of the following routines are public
 ************************************************************/
/*
 * Function:
 *	_XEditResPutString8
 *
 * Parameters:
 *	stream - stream to insert string into
 *	str    - string to insert
 *
 * Description:
 *	Inserts a string into the protocol stream.
 */
void
_XEditResPutString8(ProtocolStream *stream, _Xconst char *str)
{
    int i, len = strlen(str);

    _XEditResPut16(stream, len);
    for (i = 0; i < len; i++, str++)
	_XEditResPut8(stream, *str);
}

/*
 * Function:
 *	_XEditResPut8
 *
 * Parameters:
 *	stream - stream to insert string into
 *	value  - value to insert
 *
 * Description:
 *	Inserts an 8 bit integer into the protocol stream.
 */
void
_XEditResPut8(ProtocolStream *stream, unsigned int value)
{
    unsigned char temp;

    if (stream->size >= stream->alloc)
    {
	stream->alloc += 100;
	stream->real_top = (unsigned char *)
	    XtRealloc((char *)stream->real_top, stream->alloc + HEADER_SIZE);
	stream->top = stream->real_top + HEADER_SIZE;
	stream->current = stream->top + stream->size;
    }

    temp = (unsigned char) (value & BYTE_MASK);
    *((stream->current)++) = temp;
    (stream->size)++;
}

/*
 * Function:
 *	_XEditResPut16
 *
 * Arguments:
 *	stream - stream to insert string into
 *	value  - value to insert
 *
 * Description:
 *	Inserts a 16 bit integer into the protocol stream.
 */
void
_XEditResPut16(ProtocolStream *stream, unsigned int value)
{
    _XEditResPut8(stream, (value >> XER_NBBY) & BYTE_MASK);
    _XEditResPut8(stream, value & BYTE_MASK);
}

/*
 * Function:
 *	_XEditResPut32
 *
 * Arguments:
 *	stream - stream to insert string into
 *	value  - value to insert
 *
 * Description:
 *	Inserts a 32 bit integer into the protocol stream.
 */
void
_XEditResPut32(ProtocolStream *stream, unsigned long value)
{
    int i;

    for (i = 3; i >= 0; i--)
	_XEditResPut8(stream, (value >> (XER_NBBY * i)) & BYTE_MASK);
}

/*
 * Function:
 *	_XEditResPutWidgetInfo
 *
 * Parameters:
 *	stream - stream to insert widget info into
 *	info   - info to insert
 *
 * Description:
 *	Inserts the widget info into the protocol stream.
 */
void
_XEditResPutWidgetInfo(ProtocolStream *stream, WidgetInfo *info)
{
    unsigned int i;

    _XEditResPut16(stream, info->num_widgets);
    for (i = 0; i < info->num_widgets; i++)
	_XEditResPut32(stream, info->ids[i]);
}

/************************************************************
 * Code for retrieving values from the protocol stream
 ************************************************************/
/*
 * Function:
 *	_XEditResResetStream
 *
 * Parameters:
 *	stream - stream to reset
 *
 * Description:
 *	Resets the protocol stream.
 */
void
_XEditResResetStream(ProtocolStream *stream)
{
    stream->current = stream->top;
    stream->size = 0;
    if (stream->real_top == NULL)
    {
	stream->real_top = (unsigned char *)
	    XtRealloc((char *)stream->real_top, stream->alloc + HEADER_SIZE);
	stream->top = stream->real_top + HEADER_SIZE;
	stream->current = stream->top + stream->size;
    }
}

/*
 * NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE
 *
 * The only modified field if the "current" field
 *
 * The only fields that must be set correctly are the "current", "top"
 * and "size" fields.
 */
/*
 * Function:
 *	_XEditResGet8
 *
 * Parameters:
 *	stream - protocol stream
 *	value  - a pointer to value to return
 *
 * Description:
 *	Retrieves an unsigned 8 bit value from the protocol stream.
 *
 * Returns:
 *	True if successful
 */
Bool
_XEditResGet8(ProtocolStream *stream, unsigned char *value)
{
    if (stream->size < (unsigned long)(stream->current - stream->top))
	return (False);

    *value = *((stream->current)++);
    return (True);
}

/*
 * Function:
 *	_XEditResGet16
 *
 * Parameters:
 *	stream - protocol stream
 *	value  - pointer to return value
 *
 * Description:
 *	Retrieves an unsigned 16 bit value from the protocol stream.
 *
 * Returns:
 *	True if successful
 */
Bool
_XEditResGet16(ProtocolStream *stream, unsigned short *value)
{
    unsigned char temp1, temp2;

    if (!(_XEditResGet8(stream, &temp1) && _XEditResGet8(stream, &temp2)))
	return (False);

    *value = ((unsigned short)temp1 << XER_NBBY) + (unsigned short)temp2;
    return (True);
}

/*
 * Function:
 *	_XEditResGetSigned16
 *
 * Parameters:
 *	stream - protocol stream
 *	value  - pointer to return value
 *
 * Description:
 *	Retrieves an signed 16 bit value from the protocol stream.
 *
 * Returns:
 *	True if successful
 */
Bool
_XEditResGetSigned16(ProtocolStream *stream, short *value)
{
    unsigned char temp1, temp2;

    if (!(_XEditResGet8(stream, &temp1) && _XEditResGet8(stream, &temp2)))
	return (False);

    if (temp1 & (1 << (XER_NBBY - 1)))	/* If the sign bit is active */
    {
      *value = -1;			/* store all 1's */
      *value &= (temp1 << XER_NBBY);	/* Now and in the MSB */
      *value &= temp2;			/* and LSB */
    }
    else
	*value = ((unsigned short)temp1 << XER_NBBY) + (unsigned short)temp2;

    return (True);
}

/*
 * Function:
 *	_XEditResGet32
 *
 * Parameters:
 *	stream - protocol stream
 *	value  - pointer to return value
 *
 * Description:
 *	Retrieves an unsigned 32 bit value from the protocol stream.
 *
 * Returns:
 *	True if successful
 */
Bool
_XEditResGet32(ProtocolStream *stream, unsigned long *value)
{
    unsigned short temp1, temp2;

    if (!(_XEditResGet16(stream, &temp1) && _XEditResGet16(stream, &temp2)))
	return (False);

    *value = ((unsigned long)temp1 << (XER_NBBY * 2)) + (unsigned long)temp2;
    return (True);
}

/* Function:
 *	_XEditResGetString8
 *
 * Parameters:
 *	stream - protocol stream
 *	str    - string to retrieve
 *
 * Description:
 *	Retrieves an 8 bit string value from the protocol stream.
 *
 * Returns:
 *	True if retrieval was successful
 */
Bool
_XEditResGetString8(ProtocolStream *stream, char **str)
{
    unsigned short len;
    register unsigned i;

    if (!_XEditResGet16(stream, &len))
	return (False);

    *str = XtMalloc(sizeof(char) * (len + 1));

    for (i = 0; i < len; i++)
    {
	if (!_XEditResGet8(stream, (unsigned char *)*str + i))
	{
	    XtFree(*str);
	    *str = NULL;
	    return (False);
	}
    }
    (*str)[i] = '\0';

    return (True);
}

/*
 * Function:
 *	_XEditResGetWidgetInfo
 *
 * Parameters:
 *	stream - protocol stream
 *	info   - widget info struct to store into
 *
 * Description:
 *	  Retrieves the list of widgets that follow and stores them in the
 *	widget info structure provided.
 *
 * Returns:
 *	True if retrieval was successful
 */
Bool
_XEditResGetWidgetInfo(ProtocolStream *stream, WidgetInfo *info)
{
    unsigned int i;

    if (!_XEditResGet16(stream, &info->num_widgets))
	return (False);

    info->ids = (unsigned long *)XtMalloc(sizeof(long) * info->num_widgets);

    for (i = 0; i < info->num_widgets; i++)
    {
	if (!_XEditResGet32(stream, info->ids + i))
	{
	    XtFree((char *)info->ids);
	    info->ids = NULL;
	    return (False);
	}
#if defined(LONG64) || defined(WORD64)
	info->ids[i] |= globals.base_address;
#endif
    }
    return (True);
}

/************************************************************
 * Code for Loading the EditresBlock resource
 ************************************************************/
/*
 * Function:
 *	CvtStringToBlock
 *
 * Parameters:
 *	dpy	       - display
 *	args	       - unused
 *	num_args       - unused
 *	from_val       - value to convert
 *	to_val	       - where to store
 *	converter_data - unused
 *
 * Description:
 *	Converts a string to an editres block value.
 *
 * Returns:
 *	True if conversion was successful
 */
/*ARGSUSED*/
static Boolean
CvtStringToBlock(Display *dpy, XrmValue *args, Cardinal *num_args,
		 XrmValue *from_val, XrmValue *to_val,
		 XtPointer *converter_data)
{
    char ptr[16];
    static EditresBlock block;

    XmuNCopyISOLatin1Lowered(ptr, from_val->addr, sizeof(ptr));

    if (streq(ptr, "none"))
	block = BlockNone;
    else if (streq(ptr, "setvalues"))
	block = BlockSetValues;
    else if (streq(ptr, "all"))
	block = BlockAll;
    else
    {
	Cardinal num_params = 1;
	String params[1];

	params[0] = from_val->addr;
	XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
			"CvtStringToBlock", "unknownValue", "EditresError",
			"Could not convert string \"%s\" to EditresBlock.",
			params, &num_params);
	return FALSE;
    }

    if (to_val->addr != NULL)
    {
	if (to_val->size < sizeof(EditresBlock))
	{
	    to_val->size = sizeof(EditresBlock);
	    return FALSE;
	}
	*(EditresBlock *)(to_val->addr) = block;
    }
    else
	to_val->addr = (XtPointer)block;

    to_val->size = sizeof(EditresBlock);
    return TRUE;
}

#define XtREditresBlock		"EditresBlock"
/*
 * Function:
 *	LoadResources
 *
 * Parameters:
 *	w - any widget in the tree
 *
 * Description:
 *	  Loads a global resource the determines of this application should
 *	allow Editres requests.
 */
static void
LoadResources(Widget w)
{
    static XtResource resources[] = {
        {"editresBlock", "EditresBlock", XtREditresBlock, sizeof(EditresBlock),
	 XtOffsetOf(Globals, block), XtRImmediate, (XtPointer)BlockNone}
    };

    for (; XtParent(w) != NULL; w = XtParent(w))
	;

    XtAppSetTypeConverter(XtWidgetToApplicationContext(w),
			  XtRString, XtREditresBlock, CvtStringToBlock,
			  NULL, 0, XtCacheAll, NULL);

    XtGetApplicationResources(w, (XtPointer)&globals, resources,
			      XtNumber(resources), NULL, 0);
}

/*
 * Function:
 *	_XEditresGetStringValues
 *
 * Parameters:
 *	w	- widget
 *	warg	- where to store result
 *	numargs	- unused
 */
/*ARGSUSED*/
static void
_XEditresGetStringValues(Widget w, Arg *warg, int numargs)
{
    static char buffer[32];
    XtResourceList res_list;
    Cardinal num_res;
    XtResource *res = NULL;
    long value;
    Cardinal i;
    const char *string = "";
    Arg args[1];
    XrmValue to, from;

    /*
     * Look for the resource
     */
    XtGetResourceList(XtClass(w), &res_list, &num_res);
    for (i = 0; i < num_res; i++)
	if (strcmp(res_list[i].resource_name, warg->name) == 0)
	{
	    res = &res_list[i];
	    break;
	}

    if (res == NULL && XtParent(w) != NULL)
    {
	XtFree((char *)res_list);
	XtGetConstraintResourceList(XtClass(XtParent(w)), &res_list, &num_res);
	for (i = 0; i < num_res; i++)
	    if (strcmp(res_list[i].resource_name, warg->name) == 0)
	    {
		res = &res_list[i];
		break;
	    }
    }

    if (res == NULL)
    {
	/* Couldn't find resource */

	XtFree((char *)res_list);
	*(XtPointer *)warg->value = NULL;
	return;
    }

    /* try to get the value in the proper size */
    switch (res->resource_size)
    {
#ifdef LONG64
	long v8;
#endif
	int v4;
	short v2;
	char v1;

    case 1:
	XtSetArg(args[0], res->resource_name, &v1);
	XtGetValues(w, args, 1);
	value = (int)v1;
	break;
    case 2:
	XtSetArg(args[0], res->resource_name, &v2);
	XtGetValues(w, args, 1);
	value = (int)v2;
	break;
    case 4:
	XtSetArg(args[0], res->resource_name, &v4);
	XtGetValues(w, args, 1);
	value = (int)v4;
	break;
#ifdef LONG64
    case 8:
	XtSetArg(args[0], res->resource_name, &v8);
	XtGetValues(w, args, 1);
	value = (long)v8;
	break;
#endif
    default:
	fprintf(stderr, "_XEditresGetStringValues: bad size %d\n",
		res->resource_size);
	string = "bad size";
	*(char **)(warg->value) = (char *) string;
	XtFree((char *)res_list);
	return;
    }

    /*
     * If the resource is already String, no conversion needed
     */
    if (strcmp(XtRString, res->resource_type) == 0)
    {
	if (value == 0)
	    string = "(null)";
	else
	    string = (char *)value;
    }
    else
    {
	from.size = res->resource_size;
	from.addr = (XPointer)&value;
	to.addr = NULL;
	to.size = 0;

	if (XtConvertAndStore(w,res->resource_type, &from, XtRString, &to))
	    string = to.addr;
	else
	{
	    string = buffer;
	    /*
	     * Conversion failed, fall back to representing it as integer
	     */
	    switch (res->resource_size)
	    {
	    case sizeof(char):
		XmuSnprintf(buffer, sizeof(buffer), "%d", (int)(value & 0xff));
		break;
	    case sizeof(short):
		XmuSnprintf(buffer, sizeof(buffer), "%d", (int)(value & 0xffff));
		break;
	    case sizeof(int):
		XmuSnprintf(buffer, sizeof(buffer), "0x%08x", (int)value);
		break;
#ifdef LONG64
	    case sizeof(long):
		XmuSnprintf(buffer, sizeof(buffer), "0x%016lx", value);
		break;
#endif
	    }
	}
    }

    if (string == NULL)
	string = "";

    *(char **)(warg->value) = (char *) string;
    XtFree((char *)res_list);
}
