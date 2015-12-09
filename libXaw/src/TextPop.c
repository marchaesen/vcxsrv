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
 * This file is broken up into three sections one dealing with
 * each of the three popups created here:
 *
 * FileInsert, Search, and Replace.
 *
 * There is also a section at the end for utility functions
 * used by all more than one of these dialogs.
 *
 * The following functions are the only non-static ones defined
 * in this module.  They are located at the begining of the
 * section that contains this dialog box that uses them.
 *
 * void _XawTextInsertFileAction(w, event, params, num_params);
 * void _XawTextDoSearchAction(w, event, params, num_params);
 * void _XawTextDoReplaceAction(w, event, params, num_params);
 * void _XawTextInsertFile(w, event, params, num_params);
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xos.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xaw/TextP.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Cardinals.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Toggle.h>
#include "XawI18n.h"

static char* INSERT_FILE = "Enter Filename:";
static char* SEARCH_LABEL_1 = "Use <Tab> to change fields.";
static char* SEARCH_LABEL_2 = "Use ^q<Tab> for <Tab>.";
static char* DISMISS_NAME = "cancel";
#define DISMISS_NAME_LEN 6
static char* FORM_NAME = "form";
static char* LABEL_NAME = "label";
static char* TEXT_NAME = "text";

#define R_OFFSET      1

typedef void (*AddFunc)(Widget, char*, Widget);

/*
 * Prototypes
 */
static void _SetField(Widget, Widget);
static void AddSearchChildren(Widget, char*, Widget);
static void AddInsertFileChildren(Widget, char*, Widget);
static void CenterWidgetOnPoint(Widget, XEvent*);
static Widget CreateDialog(Widget, String, String, AddFunc);
static void DoInsert(Widget, XtPointer, XtPointer);
static void DoReplaceAll(Widget, XtPointer, XtPointer);
static void DoReplaceOne(Widget, XtPointer, XtPointer);
static Bool DoSearch(struct SearchAndReplace*);
static Widget GetShell(Widget);
static String GetString(Widget);
static String GetStringRaw(Widget);
static void InitializeSearchWidget(struct SearchAndReplace*,
				   XawTextScanDirection, Bool);
static Bool InParams(String, String*, unsigned int);
static Bool InsertFileNamed(Widget, char*);
static void PopdownFileInsert(Widget, XtPointer, XtPointer);
static void PopdownSearch(Widget, XtPointer, XtPointer);
static Bool Replace(struct SearchAndReplace*, Bool, Bool);
static void SearchButton(Widget, XtPointer, XtPointer);
static void SetResource(Widget, char*, XtArgVal);
static Bool SetResourceByName(Widget, char*, char*, XtArgVal);
static void SetSearchLabels(struct SearchAndReplace*, String, String, Bool);
static void SetWMProtocolTranslations(Widget);

/*
 * Actions
 */
static void WMProtocols(Widget, XEvent*, String*, Cardinal*);

/*
 * External Actions
 */
void _XawTextDoReplaceAction(Widget, XEvent*, String*, Cardinal*);
void _XawTextDoSearchAction(Widget, XEvent*, String*, Cardinal*);
void _XawTextInsertFile(Widget, XEvent*, String*, Cardinal*);
void _XawTextInsertFileAction(Widget, XEvent*, String*, Cardinal*);
void _XawTextPopdownSearchAction(Widget, XEvent*, String*, Cardinal*);
void _XawTextSearch(Widget, XEvent*, String*, Cardinal*);
void _XawTextSetField(Widget, XEvent*, String*, Cardinal*);

/*
 * From Text.c
 */
char *_XawTextGetText(TextWidget, XawTextPosition, XawTextPosition);
void _XawTextShowPosition(TextWidget);

/*
 * Initialization
 */
static char radio_trans_string[] =
"<Btn1Down>,<Btn1Up>:"	"set() notify()\n"
;

static char search_text_trans[] =
"~s<Key>Return:"	"DoSearchAction(Popdown)\n"
"s<Key>Return:"		"DoSearchAction() SetField(Replace)\n"
"c<Key>c:"		"PopdownSearchAction()\n"
"<Btn1Down>:"		"select-start() SetField(Search)\n"
"<Key>Tab:"		"DoSearchAction() SetField(Replace)\n"
;

static char rep_text_trans[] =
"~s<Key>Return:"	"DoReplaceAction(Popdown)\n"
"s<Key>Return:"		"SetField(Search)\n"
"c<Key>c:"		"PopdownSearchAction()\n"
"<Btn1Down>:"		"select-start() DoSearchAction() SetField(Replace)\n"
"<Key>Tab:"		"SetField(Search)\n"
;

/*
 * Implementation
 */
/*
 * This section of the file contains all the functions that
 * the file insert dialog box uses
 */

/*
 * Function:
 *	_XawTextInsertFileAction
 *
 * Description:
 *	  Action routine that can be bound to dialog box's Text Widget
 *	that will insert a file into the main Text Widget.
 */
/*ARGSUSED*/
void
_XawTextInsertFileAction(Widget w, XEvent *event,
			 String *params, Cardinal *num_params)
{
    DoInsert(w, (XtPointer)XtParent(XtParent(XtParent(w))), NULL);
}

/*
 * Function:
 *	_XawTextInsertFile
 *
 * Parameters:
 *	w	   - text widget
 *	event	   - X Event (used to get x and y location)
 *	params	   - parameter list
 *	num_params - ""
 *
 * Description:
 *	  Action routine that can be bound to the text widget
 *	it will popup the insert file dialog box.
 *
 * Note:
 *	The parameter list may contain one entry
 *
 *  Entry:
 *	  This entry is optional and contains the value of the default
 *	file to insert
 */
void
_XawTextInsertFile(Widget w, XEvent *event,
		   String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    char *ptr;
    XawTextEditType edit_mode;
    Arg args[1];

    XtSetArg(args[0], XtNeditType, &edit_mode);
    XtGetValues(ctx->text.source, args, 1);

    if (edit_mode != XawtextEdit) {
	XBell(XtDisplay(w), 0);
	return;
    }

    if (*num_params == 0)
	ptr = "";
    else
	ptr = params[0];

    if (!ctx->text.file_insert) {
	ctx->text.file_insert = CreateDialog(w, ptr, "insertFile",
					     AddInsertFileChildren);
	XtRealizeWidget(ctx->text.file_insert);
	SetWMProtocolTranslations(ctx->text.file_insert);
    }

    CenterWidgetOnPoint(ctx->text.file_insert, event);
    XtPopup(ctx->text.file_insert, XtGrabNone);
}

/*
 * Function:
 *	PopdownFileInsert
 *
 * Parameters:
 *	w	  - widget that caused this action
 *	closure	  - pointer to the main text widget that popped up this dialog
 *	call_data - (not used)
 *
 * Description:
 *	Pops down the file insert button
 */
/*ARGSUSED*/
static void
PopdownFileInsert(Widget w, XtPointer closure, XtPointer call_data)
{
    TextWidget ctx = (TextWidget)closure;

    XtPopdown(ctx->text.file_insert);
    (void)SetResourceByName(ctx->text.file_insert, LABEL_NAME,
			    XtNlabel, (XtArgVal)INSERT_FILE);
}

/*
 * Function:
 *	DoInsert
 *
 * Parameters:
 *	w	- widget that activated this callback
 *	closure - pointer to the text widget to insert the file into
 *
 * Description:
 *	Actually insert the file named in the text widget of the file dialog
 */
/*ARGSUSED*/
static void
DoInsert(Widget w, XtPointer closure, XtPointer call_data)
{
    TextWidget ctx = (TextWidget)closure;
    char buf[BUFSIZ], msg[BUFSIZ];
    Widget temp_widget;

    snprintf(buf, sizeof(buf), "%s.%s", FORM_NAME, TEXT_NAME);
    if ((temp_widget = XtNameToWidget(ctx->text.file_insert, buf)) == NULL) {
	(void)strcpy(msg,
		     "Error: Could not get text widget from file insert popup");
    }
    else if (InsertFileNamed((Widget)ctx, GetString(temp_widget))) {
	PopdownFileInsert(w, closure, call_data);
	return;
    }
    else
	snprintf(msg, sizeof(msg), "Error: %s", strerror(errno));

    (void)SetResourceByName(ctx->text.file_insert,
			    LABEL_NAME, XtNlabel, (XtArgVal)msg);
    XBell(XtDisplay(w), 0);
}

/*
 * Function:
 *	InsertFileNamed
 *
 * Parameters:
 *	tw  - text widget to insert this file into
 *	str - name of the file to insert
 *
 * Description:
 *	Inserts a file into the text widget.
 *
 * Returns:
 *	True if the insert was sucessful, False otherwise.
 */
static Bool
InsertFileNamed(Widget tw, char *str)
{
    FILE *file;
    XawTextBlock text;
    XawTextPosition pos;

    if (str == NULL || strlen(str) == 0 || (file = fopen(str, "r")) == NULL)
	return (False);

    pos = XawTextGetInsertionPoint(tw);

    fseek(file, 0L, SEEK_END);

    text.firstPos = 0;
    text.length = ftell(file);
    text.ptr = XtMalloc(text.length + 1);
    text.format = XawFmt8Bit;

    fseek(file, 0L, SEEK_SET);
    if (fread(text.ptr, 1, text.length, file) != text.length)
	XtErrorMsg("readError", "insertFileNamed", "XawError",
		   "fread returned error", NULL, NULL);

    if (XawTextReplace(tw, pos, pos, &text) != XawEditDone) {
	XtFree(text.ptr);
	fclose(file);
	return (False);
    }
    pos += text.length;
    XtFree(text.ptr);
    fclose(file);
    XawTextSetInsertionPoint(tw, pos);
    _XawTextShowPosition((TextWidget)tw);

    return (True);
}

/*
 * Function:
 *	AddInsertFileChildren
 *
 * Parameters:
 *	form - form widget for the insert dialog widget
 *	ptr  - pointer to the initial string for the Text Widget
 *	tw   - main text widget
 *
 * Description:
 *	Adds all children to the InsertFile dialog widget.
 */
static void
AddInsertFileChildren(Widget form, char *ptr, Widget tw)
{
    Arg args[10];
    Cardinal num_args;
    Widget label, text, cancel, insert;
    XtTranslations trans;

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, INSERT_FILE);	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);	num_args++;
    XtSetArg(args[num_args], XtNresizable, True);	num_args++;
    XtSetArg(args[num_args], XtNborderWidth, 0);	num_args++;
    label = XtCreateManagedWidget(LABEL_NAME, labelWidgetClass, form,
				  args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNfromVert, label);	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	num_args++;
    XtSetArg(args[num_args], XtNright, XtChainRight);	num_args++;
    XtSetArg(args[num_args], XtNeditType, XawtextEdit);	num_args++;
    XtSetArg(args[num_args], XtNresizable, True);	num_args++;
    XtSetArg(args[num_args], XtNstring, ptr);		num_args++;
    text = XtCreateManagedWidget(TEXT_NAME, asciiTextWidgetClass, form,
				 args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Insert File");	num_args++;
    XtSetArg(args[num_args], XtNfromVert, text);	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);	num_args++;
    insert = XtCreateManagedWidget("insert", commandWidgetClass, form,
				   args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Cancel");	num_args++;
    XtSetArg(args[num_args], XtNfromVert, text);	num_args++;
    XtSetArg(args[num_args], XtNfromHoriz, insert);	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);	num_args++;
    cancel = XtCreateManagedWidget(DISMISS_NAME, commandWidgetClass, form,
				   args, num_args);

    XtAddCallback(cancel, XtNcallback, PopdownFileInsert, (XtPointer)tw);
    XtAddCallback(insert, XtNcallback, DoInsert, (XtPointer)tw);

    XtSetKeyboardFocus(form, text);

    /*
     * Bind <CR> to insert file
     */
    trans = XtParseTranslationTable("<Key>Return:InsertFileAction()");
    XtOverrideTranslations(text, trans);
}

/*
 * This section of the file contains all the functions that
 * the search dialog box uses
 */
/*
 * Function:
 *	_XawTextDoSearchAction
 *
 * Description:
 *	  Action routine that can be bound to dialog box's Text Widget that
 *	will search for a string in the main Text Widget.
 *
 * Note:
 * If the search was sucessful and the argument popdown is passed to
 * this action routine then the widget will automatically popdown the
 *	search widget
 */
/*ARGSUSED*/
void
_XawTextDoSearchAction(Widget w, XEvent *event,
		       String *params, Cardinal *num_params)
{
    TextWidget tw = (TextWidget)XtParent(XtParent(XtParent(w)));
    Bool popdown = False;

    if (*num_params == 1 && (params[0][0] == 'p' || params[0][0] == 'P'))
	popdown = True;

    if (DoSearch(tw->text.search) && popdown)
	PopdownSearch(w, (XtPointer)tw->text.search, NULL);
}

/*
 * Function:
 *	_XawTextPopdownSearchAction
 *
 * Description:
 *	  Action routine that can be bound to dialog box's Text Widget that
 *	will popdown the search widget.
 */
/*ARGSUSED*/
void
_XawTextPopdownSearchAction(Widget w, XEvent *event,
			    String *params, Cardinal *num_params)
{
    TextWidget tw = (TextWidget)XtParent(XtParent(XtParent(w)));

    PopdownSearch(w, (XtPointer)tw->text.search, NULL);
}

/*
 * Function:
 *	PopdownSearch
 *
 * Parameters:
 *	w	  - (not used)
 *	closure	  - pointer to the search structure
 *	call_data - (not used)
 *
 * Description:
 *	Pops down the search widget and resets it
 */
/*ARGSUSED*/
static void
PopdownSearch(Widget w, XtPointer closure, XtPointer call_data)
{
    struct SearchAndReplace *search = (struct SearchAndReplace *)closure;

    XtPopdown(search->search_popup);
    SetSearchLabels(search, SEARCH_LABEL_1, SEARCH_LABEL_2, False);
}

/*
 * Function:
 *	SearchButton
 *
 * Arguments:
 *	w	  - (not used)
 *	closure	  - pointer to the search info
 *	call_data - (not used)
 *
 * Description:
 *	Performs a search when the button is clicked.
 */
/*ARGSUSED*/
static void
SearchButton(Widget w, XtPointer closure, XtPointer call_data)
{
    (void)DoSearch((struct SearchAndReplace *)closure);
}

/*
 * Function:
 *	_XawTextSearch
 *
 * Parameters:
 *	w	   - text widget
 *	event	   - X Event (used to get x and y location)
 *	params	   - parameter list
 *	num_params - ""
 *
 * Description:
 *	  Action routine that can be bound to the text widget
 *	it will popup the search dialog box.
 *
 * Note:
 *	  The parameter list contains one or two entries that may be
 *	the following.
 *
 * First Entry:
 *	  The first entry is the direction to search by default.
 *	This arguement must be specified and may have a value of
 *	"left" or "right".
 *
 * Second Entry:
 *	  This entry is optional and contains the value of the default
 *	string to search for.
 */

#define SEARCH_HEADER	"Text Widget - Search():"
void
_XawTextSearch(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    XawTextScanDirection dir;
    char *ptr, buf[BUFSIZ];
    XawTextEditType edit_mode;
    Arg args[1];
    wchar_t wcs[1];

    if (*num_params < 1 || *num_params > 2) {
	snprintf(buf, sizeof(buf), "%s %s\n%s", SEARCH_HEADER,
		 "This action must have only",
		 "one or two parameters");
	XtAppWarning(XtWidgetToApplicationContext(w), buf);
	return;
    }

    if (*num_params == 2)
	ptr = params[1];
    else if (XawTextFormat(ctx, XawFmtWide)) {
	/* This just does the equivalent of
	   ptr = ""L, a waste because params[1] isnt W aligned */
	ptr = (char *)wcs;
	wcs[0] = 0;
    }
    else
	ptr = "";

    switch(params[0][0]) {
	case 'b':		  /* Left */
	case 'B':
	    dir = XawsdLeft;
	    break;
	case 'f':		  /* Right */
	case 'F':
	    dir = XawsdRight;
	    break;
	default:
	    snprintf(buf, sizeof(buf), "%s %s\n%s", SEARCH_HEADER,
		     "The first parameter must be",
		     "Either 'backward' or 'forward'");
	    XtAppWarning(XtWidgetToApplicationContext(w), buf);
	    return;
    }

    if (ctx->text.search== NULL) {
	ctx->text.search = XtNew(struct SearchAndReplace);
	ctx->text.search->search_popup = CreateDialog(w, ptr, "search",
						      AddSearchChildren);
	XtRealizeWidget(ctx->text.search->search_popup);
	SetWMProtocolTranslations(ctx->text.search->search_popup);
    }
    else if (*num_params > 1)
	XtVaSetValues(ctx->text.search->search_text, XtNstring, ptr, NULL);

    XtSetArg(args[0], XtNeditType,&edit_mode);
    XtGetValues(ctx->text.source, args, 1);

    InitializeSearchWidget(ctx->text.search, dir, (edit_mode == XawtextEdit));

    CenterWidgetOnPoint(ctx->text.search->search_popup, event);
    XtPopup(ctx->text.search->search_popup, XtGrabNone);
}

/*
 * Function:
 *	InitializeSearchWidget
 *
 * Parameters:
 *	search	       - search widget structure
 *	dir	       - direction to search
 *	replace_active - state of the sensitivity for the replace button
 *
 * Description:
 *	  This function initializes the search widget and
 *		   is called each time the search widget is poped up.
 */
static void
InitializeSearchWidget(struct SearchAndReplace *search,
		       XawTextScanDirection dir, Bool replace_active)
{
    SetResource(search->rep_one, XtNsensitive, (XtArgVal)replace_active);
    SetResource(search->rep_all, XtNsensitive, (XtArgVal)replace_active);
    SetResource(search->rep_label, XtNsensitive, (XtArgVal)replace_active);
    SetResource(search->rep_text, XtNsensitive, (XtArgVal)replace_active);

    switch (dir) {
	case XawsdLeft:
	    SetResource(search->left_toggle, XtNstate, (XtArgVal)True);
	    break;
	case XawsdRight:
	    SetResource(search->right_toggle, XtNstate, (XtArgVal)True);
	    break;
    }
}

/*
 * Function:
 *	AddSearchChildren
 *
 * Parameters:
 *	form - form widget for the search widget
 *	ptr  - pointer to the initial string for the Text Widget
 *	tw   - main text widget
 *
 * Description:
 *	Adds all children to the Search Dialog Widget.
 */
static void
AddSearchChildren(Widget form, char *ptr, Widget tw)
{
    Arg args[10];
    Cardinal num_args;
    Widget cancel, search_button, s_label, s_text, r_text;
    XtTranslations trans;
    struct SearchAndReplace *search = ((TextWidget)tw)->text.search;

    num_args = 0;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);	num_args++;
    XtSetArg(args[num_args], XtNresizable, True);	num_args++;
    XtSetArg(args[num_args], XtNborderWidth, 0);	num_args++;
    search->label1 = XtCreateManagedWidget("label1", labelWidgetClass, form,
					   args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNfromVert, search->label1); num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	   num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);	   num_args++;
    XtSetArg(args[num_args], XtNresizable, True);	   num_args++;
    XtSetArg(args[num_args], XtNborderWidth, 0);	   num_args++;
    search->label2 = XtCreateManagedWidget("label2", labelWidgetClass, form,
					   args, num_args);

    /*
     * We need to add R_OFFSET to the radio_data, because the value zero (0)
     * has special meaning
     */
    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Backward");	   num_args++;
    XtSetArg(args[num_args], XtNfromVert, search->label2); num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	   num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);	   num_args++;
    XtSetArg(args[num_args], XtNradioData, (XPointer)XawsdLeft + R_OFFSET);
    num_args++;
    search->left_toggle = XtCreateManagedWidget("backwards", toggleWidgetClass,
						form, args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Forward");	   num_args++;
    XtSetArg(args[num_args], XtNfromVert, search->label2); num_args++;
    XtSetArg(args[num_args], XtNfromHoriz, search->left_toggle); num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);	   num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);	   num_args++;
    XtSetArg(args[num_args], XtNradioGroup, search->left_toggle); num_args++;
    XtSetArg(args[num_args], XtNradioData, (XPointer)XawsdRight + R_OFFSET);
    num_args++;
    search->right_toggle = XtCreateManagedWidget("forwards", toggleWidgetClass,
						 form, args, num_args);

    {
	XtTranslations radio_translations;

	radio_translations = XtParseTranslationTable(radio_trans_string);
	XtOverrideTranslations(search->left_toggle, radio_translations);
	XtOverrideTranslations(search->right_toggle, radio_translations);
    }

#ifndef OLDXAW
    if (XawTextFormat((TextWidget)tw, XawFmt8Bit)) {
	num_args = 0;
	XtSetArg(args[num_args], XtNlabel, "Case Sensitive");	num_args++;
	XtSetArg(args[num_args], XtNfromVert, search->label2);	num_args++;
	XtSetArg(args[num_args], XtNfromHoriz, search->right_toggle); num_args++;
	XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
	XtSetArg(args[num_args], XtNright, XtChainLeft);	num_args++;
	XtSetArg(args[num_args], XtNstate, True);		num_args++;
	search->case_sensitive = XtCreateManagedWidget("case", toggleWidgetClass,
						       form, args, num_args);
    }
    else
	search->case_sensitive = NULL;
#endif

    num_args = 0;
    XtSetArg(args[num_args], XtNfromVert, search->left_toggle);	num_args++;
    XtSetArg(args[num_args], XtNlabel, "Search for:  ");	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNborderWidth, 0 );		num_args++;
    s_label = XtCreateManagedWidget("searchLabel", labelWidgetClass, form,
				    args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNfromVert, search->left_toggle); num_args++;
    XtSetArg(args[num_args], XtNfromHoriz, s_label);		num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainRight);		num_args++;
    XtSetArg(args[num_args], XtNeditType, XawtextEdit);		num_args++;
    XtSetArg(args[num_args], XtNresizable, True);		num_args++;
    XtSetArg(args[num_args], XtNstring, ptr);			num_args++;
    s_text = XtCreateManagedWidget("searchText", asciiTextWidgetClass, form,
				   args, num_args);
    search->search_text = s_text;

    num_args = 0;
    XtSetArg(args[num_args], XtNfromVert, s_text);		num_args++;
    XtSetArg(args[num_args], XtNlabel, "Replace with:");	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNborderWidth, 0);		num_args++;
    search->rep_label = XtCreateManagedWidget("replaceLabel", labelWidgetClass,
					      form, args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNfromHoriz, s_label);		num_args++;
    XtSetArg(args[num_args], XtNfromVert, s_text);		num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainRight);		num_args++;
    XtSetArg(args[num_args], XtNeditType, XawtextEdit);		num_args++;
    XtSetArg(args[num_args], XtNresizable, True);		num_args++;
    XtSetArg(args[num_args], XtNstring, ""); num_args++;
    r_text = XtCreateManagedWidget("replaceText", asciiTextWidgetClass,
				   form, args, num_args);
    search->rep_text = r_text;

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Search");		num_args++;
    XtSetArg(args[num_args], XtNfromVert, r_text);		num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);		num_args++;
    search_button = XtCreateManagedWidget("search", commandWidgetClass, form,
					  args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Replace");		num_args++;
    XtSetArg(args[num_args], XtNfromVert, r_text);		num_args++;
    XtSetArg(args[num_args], XtNfromHoriz, search_button);	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);		num_args++;
    search->rep_one = XtCreateManagedWidget("replaceOne", commandWidgetClass,
					    form, args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Replace All");		num_args++;
    XtSetArg(args[num_args], XtNfromVert, r_text);		num_args++;
    XtSetArg(args[num_args], XtNfromHoriz, search->rep_one);	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);		num_args++;
    search->rep_all = XtCreateManagedWidget("replaceAll", commandWidgetClass,
					    form, args, num_args);

    num_args = 0;
    XtSetArg(args[num_args], XtNlabel, "Cancel");		num_args++;
    XtSetArg(args[num_args], XtNfromVert, r_text);		num_args++;
    XtSetArg(args[num_args], XtNfromHoriz, search->rep_all);	num_args++;
    XtSetArg(args[num_args], XtNleft, XtChainLeft);		num_args++;
    XtSetArg(args[num_args], XtNright, XtChainLeft);		num_args++;
    cancel = XtCreateManagedWidget(DISMISS_NAME, commandWidgetClass, form,
				   args, num_args);

    XtAddCallback(search_button, XtNcallback, SearchButton, (XtPointer)search);
    XtAddCallback(search->rep_one, XtNcallback, DoReplaceOne, (XtPointer)search);
    XtAddCallback(search->rep_all, XtNcallback, DoReplaceAll, (XtPointer)search);
    XtAddCallback(cancel, XtNcallback, PopdownSearch, (XtPointer)search);

    /*
     * Initialize the text entry fields
     */
    {
	Pixel color;

	num_args = 0;
	XtSetArg(args[num_args], XtNbackground, &color); num_args++;
	XtGetValues(search->rep_text, args, num_args);
	num_args = 0;
	XtSetArg(args[num_args], XtNborderColor, color); num_args++;
	XtSetValues(search->rep_text, args, num_args);
	XtSetKeyboardFocus(form, search->search_text);
    }

    SetSearchLabels(search, SEARCH_LABEL_1, SEARCH_LABEL_2, False);

    /*
     * Bind Extra translations
     */
    trans = XtParseTranslationTable(search_text_trans);
    XtOverrideTranslations(search->search_text, trans);

    trans = XtParseTranslationTable(rep_text_trans);
    XtOverrideTranslations(search->rep_text, trans);
}

/*
 * Function:
 *	DoSearch
 *
 * Parameters:
 *	search - search structure
 *
 * Description:
 *	Performs a search
 *
 * Returns:
 *	True if sucessful
 */
/*ARGSUSED*/
static Bool
DoSearch(struct SearchAndReplace *search)
{
    char msg[37];
    Widget tw = XtParent(search->search_popup);
    XawTextPosition pos;
    XawTextScanDirection dir;
    XawTextBlock text;
    TextWidget ctx = (TextWidget)tw;

    text.firstPos = 0;
    text.ptr = GetStringRaw(search->search_text);
    if ((text.format = _XawTextFormat(ctx)) == XawFmtWide)
	text.length = wcslen((wchar_t*)text.ptr);
    else {
	text.length = strlen(text.ptr);

#ifndef OLDXAW
	if (search->case_sensitive) {
	  /* text.firstPos isn't useful here, so I'll use it as an
	   * options flag.
	   */
	    Arg args[1];
	    Boolean case_sensitive;

	    XtSetArg(args[0], XtNstate, &case_sensitive);
	    XtGetValues(search->case_sensitive, args, 1);
	    text.firstPos = !case_sensitive;
	}
#endif /* OLDXAW */
    }

    dir = (XawTextScanDirection)(unsigned long)
      ((XPointer)XawToggleGetCurrent(search->left_toggle) - R_OFFSET);

    pos = XawTextSearch(tw, dir, &text);

   /* The Raw string in find.ptr may be WC I can't use here, so I re - call
     GetString to get a tame version */

    if (pos == XawTextSearchError) {
	char *ptr;
	int len;

	ptr = GetString(search->search_text);
	len = strlen(ptr);
	snprintf(msg, sizeof(msg), "%s", ptr);

	ptr = strchr(msg, '\n');
	if (ptr != NULL || sizeof(msg) - 1 < len) {
	    if (ptr != NULL)
		len = ptr - msg + 4;
	    else
		len = strlen(msg);

	    if (len < 4)
		strcpy(msg, "...");
	    else
		strcpy(msg + len - 4, "...");
	}
	XawTextUnsetSelection(tw);
	SetSearchLabels(search, "Could not find string", msg, True);

	return (False);
    }
    XawTextDisableRedisplay(tw);
    XawTextSetSelection(tw, pos, pos + text.length);
    search->selection_changed = False;	/* selection is good */

    if (dir == XawsdRight)
	XawTextSetInsertionPoint(tw, pos + text.length);
    else
	XawTextSetInsertionPoint(tw, pos);
    _XawTextShowPosition(ctx);
    XawTextEnableRedisplay(tw);

    return (True);
}

/*
 * This section of the file contains all the functions that
 * the replace dialog box uses
 */
/*
 * Function:
 *	_XawTextDoReplaceAction
 *
 * Description:
 *	Action routine that can be bound to dialog box's
 *	Text Widget that will replace a string in the main Text Widget.
 */
/*ARGSUSED*/
void
_XawTextDoReplaceAction(Widget w, XEvent *event,
			String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)XtParent(XtParent(XtParent(w)));
    Bool popdown = False;

    if (*num_params == 1 && (params[0][0] == 'p' || params[0][0] == 'P'))
	popdown = True;

    if (Replace( ctx->text.search, True, popdown) && popdown)
	PopdownSearch(w, (XtPointer)ctx->text.search, NULL);
}

/*
 * Function:
 *	DoReplaceOne
 *
 * Arguments:
 *	w	  - *** Not Used ***
 *	closure   - a pointer to the search structure
 *	call_data - *** Not Used ***
 *
 * Description:
 *	  Replaces the first instance of the string in the search
 *	dialog's text widget with the one in the replace dialog's text widget.
 */
/*ARGSUSED*/
static void
DoReplaceOne(Widget w, XtPointer closure, XtPointer call_data)
{
    Replace((struct SearchAndReplace *)closure, True, False);
}

/*
 * Function:
 *	DoReplaceAll
 *
 * Parameters:
 *	w	  - (not used)
 *	closure	  - pointer to the search structure
 *	call_data - (not used)
 *
 * Description:
 *	  Replaces every instance of the string in the search dialog's
 *	text widget with the one in the replace dialog's text widget.
 */
/*ARGSUSED*/
static void
DoReplaceAll(Widget w, XtPointer closure, XtPointer call_data)
{
    Replace((struct SearchAndReplace *)closure, False, False);
}

/*
 * Function:
 *	Replace
 *
 * Parameters:
 *	tw	      - Text Widget to replce the string in
 *	once_only     - if True then only replace the first one found,
 *			else replace all of them
 *	show_current  - if true then leave the selection on the
 *		        string that was just replaced, otherwise
 *		        move it onto the next one
 *
 * Description:
 *	  This is the function that does the real work of
 *	replacing strings in the main text widget.
 */
static Bool
Replace(struct SearchAndReplace *search, Bool once_only, Bool show_current)
{
    XawTextPosition pos, new_pos, end_pos, ipos;
    XawTextScanDirection dir;
    XawTextBlock find, replace;
    Widget tw = XtParent(search->search_popup);
    int count = 0;
    TextWidget ctx = (TextWidget)tw;
    Bool redisplay;

    find.ptr = GetStringRaw(search->search_text);
    if ((find.format = _XawTextFormat(ctx)) == XawFmtWide)
	find.length = (XawTextPosition)wcslen((wchar_t*)find.ptr);
    else
	find.length = (XawTextPosition)strlen(find.ptr);
    find.firstPos = 0;

    replace.ptr = GetStringRaw(search->rep_text);
    replace.firstPos = 0;
    if ((replace.format = _XawTextFormat(ctx)) == XawFmtWide)
	replace.length = wcslen((wchar_t*)replace.ptr);
    else
	replace.length = strlen(replace.ptr);

    dir = (XawTextScanDirection)(unsigned long)
      ((XPointer)XawToggleGetCurrent(search->left_toggle) - R_OFFSET);

    redisplay = !once_only || (once_only && !show_current);
    ipos = XawTextGetInsertionPoint(tw);
    if (redisplay)
	XawTextDisableRedisplay(tw);
    /*CONSTCOND*/
    while (True) {
	if (count != 0)	{
	    new_pos = XawTextSearch(tw, dir, &find);

	    if (new_pos == XawTextSearchError) {
		if (count == 0) {
		    char msg[37];
		    char *ptr;
		    int len;

		    ptr = GetString(search->search_text);
		    len = strlen(ptr);
		    snprintf(msg, sizeof(msg), "%s", ptr);
		    ptr = strchr(msg, '\n');
		    if (ptr != NULL || sizeof(msg) - 1 < len) {
			if (ptr != NULL)
			    len = ptr - msg + 4;
			else
			    len = strlen(msg);

			if (len < 4)
			    strcpy(msg, "...");
			else
			    strcpy(msg + len - 4, "...");
		    }
		    SetSearchLabels(search, "Could not find string", msg, True);

		    if (redisplay) {
			XawTextSetInsertionPoint(tw, ipos);
			_XawTextShowPosition(ctx);
			XawTextEnableRedisplay(tw);
		    }

		    return (False);
		}
		else
		    break;
	    }
	    pos = new_pos;
	    end_pos = pos + find.length;
	}
	else {
	    XawTextGetSelectionPos(tw, &pos, &end_pos);

	    if (search->selection_changed) {
		SetSearchLabels(search, "Selection modified, aborting.",
				"", True);
		if (redisplay) {
		    XawTextSetInsertionPoint(tw, ipos);
		    XawTextEnableRedisplay(tw);
		}

		return (False);
	    }
	    if (pos == end_pos) {
		if (redisplay) {
		    XawTextSetInsertionPoint(tw, ipos);
		    XawTextEnableRedisplay(tw);
		}

		return (False);
	    }
	}

	if (XawTextReplace(tw, pos, end_pos, &replace) != XawEditDone) {
	    SetSearchLabels(search, "Error while replacing.", "", True);
	    if (redisplay) {
		XawTextSetInsertionPoint(tw, ipos);
		XawTextEnableRedisplay(tw);
	    }

	    return (False);
	}

	if (dir == XawsdRight)
	    ipos = pos + replace.length;
	else
	    ipos = pos;

	if (once_only) {
	    if (show_current)
		break;
	    else {
		DoSearch(search);
		XawTextEnableRedisplay(tw);

		return (True);
	    }
	}
	else
	    ctx->text.insertPos = ipos;
	count++;
    }

    if (replace.length == 0)
	XawTextUnsetSelection(tw);
    else
	XawTextSetSelection(tw, pos, pos + replace.length);

    XawTextSetInsertionPoint(tw, ipos);
    _XawTextShowPosition(ctx);
    XawTextEnableRedisplay(tw);

    return (True);
}

/*
 * Function:
 *	SetSearchLabels
 *
 * Parameters:
 *	search - search structure
 *	msg1   - message to put in each search label
 *	msg2   - ""
 *	bell   - if True then ring bell
 *
 * Description:
 *	Sets both the search labels, and also rings the bell.
 */
static void
SetSearchLabels(struct SearchAndReplace *search, String msg1, String msg2,
		Bool bell)
{
    (void)SetResource(search->label1, XtNlabel, (XtArgVal)msg1);
    (void)SetResource(search->label2, XtNlabel, (XtArgVal)msg2);
    if (bell)
	XBell(XtDisplay(search->search_popup), 0);
}

/*
 * This section of the file contains utility routines used by
 * other functions in this file
 */
/*
 * Function:
 *	_XawTextSetField
 *
 * Description:
 *	  Action routine that can be bound to dialog box's
 *		   Text Widget that will send input to the field specified.
 */
/*ARGSUSED*/
void
_XawTextSetField(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    struct SearchAndReplace *search;
    Widget cnew, old;

    search = ((TextWidget)XtParent(XtParent(XtParent(w))))->text.search;

    if (*num_params != 1) {
	SetSearchLabels(search, "Error: SetField Action must have",
			"exactly one argument", True);
	return;
    }
    switch (params[0][0])  {
	case 's':
	case 'S':
	    cnew = search->search_text;
	    old = search->rep_text;
	    break;
	case 'r':
	case 'R':
	    old = search->search_text;
	    cnew = search->rep_text;
	    break;
	default:
	    SetSearchLabels(search,
			    "Error: SetField Action's first Argument must",
			    "be either 'Search' or 'Replace'", True);
	    return;
    }
    _SetField(cnew, old);
}

/*
 * Function:
 *	_SetField
 *
 * Parameters:
 *	cnew - new and old text fields
 *	old  - ""
 *
 * Description:
 *	Sets the current text field.
 */
static void
_SetField(Widget cnew, Widget old)
{
    Arg args[2];
    Pixel new_border, old_border, old_bg;

    if (!XtIsSensitive(cnew)) {
	XBell(XtDisplay(old), 0);	/* Don't set field to an inactive Widget */
	return;
    }

    XtSetKeyboardFocus(XtParent(cnew), cnew);

    XtSetArg(args[0], XtNborderColor, &old_border);
    XtSetArg(args[1], XtNbackground, &old_bg);
    XtGetValues(cnew, args, 2);

    XtSetArg(args[0], XtNborderColor, &new_border);
    XtGetValues(old, args, 1);

    if (old_border != old_bg)	/* Colors are already correct, return */
	return;

    SetResource(old, XtNborderColor, (XtArgVal)old_border);
    SetResource(cnew, XtNborderColor, (XtArgVal)new_border);
}

/*
 * Function:
 *	SetResourceByName
 *
 * Parameters:
 *	shell	 - shell widget of the popup
 *	name	 - name of the child
 *	res_name - name of the resource
 *	value	 - value of the resource
 *
 * Description:
 *	  Sets a resource in any of the dialog children given
 *	name of the child and the shell widget of the dialog.
 *
 * Returns:
 *	True if sucessful
 */
static Bool
SetResourceByName(Widget shell, char *name, char *res_name, XtArgVal value)
{
    Widget temp_widget;
    char buf[BUFSIZ];

    snprintf(buf, sizeof(buf), "%s.%s", FORM_NAME, name);

    if ((temp_widget = XtNameToWidget(shell, buf)) != NULL) {
	SetResource(temp_widget, res_name, value);
	return (True);
    }
    return (False);
}

/*
 * Function:
 *	SetResource
 *
 * Parameters:
 *	w	 - widget
 *	res_name - name of the resource
 *	value	 - value of the resource
 *
 * Description:
 *	Sets a resource in a widget
 */
static void
SetResource(Widget w, char *res_name, XtArgVal value)
{
    Arg args[1];

    XtSetArg(args[0], res_name, value);
    XtSetValues( w, args, 1);
}

/*
 * Function:
 *	GetString{Raw}
 *
 * Parameters:
 *	text - text widget whose string we will get
 *
 * Description:
 *	Gets the value for the string in the popup.
 *
 * Returns:
 *	GetString:	the string as a MB
 *	GetStringRaw:	the exact buffer contents suitable for a search
 */
static String
GetString(Widget text)
{
    String string;
    Arg args[1];

    XtSetArg(args[0], XtNstring, &string);
    XtGetValues(text, args, 1);

    return (string);
}

static String
GetStringRaw(Widget tw)
{
    TextWidget ctx = (TextWidget)tw;
    XawTextPosition last;

    last = XawTextSourceScan(ctx->text.source, 0, XawstAll, XawsdRight,
			     ctx->text.mult, True);
    return (_XawTextGetText(ctx, 0, last));
}

/*
 * Function:
 *	CenterWidgetOnPoint
 *
 * Parameters:
 *	w     - shell widget
 *		 event - event containing the location of the point
 *
 * Description:
 *	Centers a shell widget on a point relative to the root window.
 *
 * Note:
 *	The widget is not allowed to go off the screen
 */
static void
CenterWidgetOnPoint(Widget w, XEvent *event)
{
    Arg args[3];
    Cardinal num_args;
    Dimension width, height, b_width;
    Position x, y, max_x, max_y;

    if (event != NULL) {
	switch (event->type) {
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
	    default:
		return;
	}
    }
    else
	return;

    num_args = 0;
    XtSetArg(args[num_args], XtNwidth, &width); num_args++;
    XtSetArg(args[num_args], XtNheight, &height); num_args++;
    XtSetArg(args[num_args], XtNborderWidth, &b_width); num_args++;
    XtGetValues(w, args, num_args);

    width += b_width << 1;
    height += b_width << 1;

    x -= (Position)(width >> 1);
    if (x < 0)
	x = 0;
    if (x > (max_x = (Position)(XtScreen(w)->width - width)))
	x = max_x;

    y -= (Position)(height >> 1);
    if (y < 0)
	y = 0;
    if (y > (max_y = (Position)(XtScreen(w)->height - height)))
	y = max_y;

    num_args = 0;
    XtSetArg(args[num_args], XtNx, x); num_args++;
    XtSetArg(args[num_args], XtNy, y); num_args++;
    XtSetValues(w, args, num_args);
}

/*
 * Function:
 *	CreateDialog
 *
 * Parameters:
 *	parent - parent of the dialog - the main text widget
 *	ptr    - initial_string for the dialog
 *	name   - name of the dialog
 *	func   - function to create the children of the dialog
 *
 * Returns:
 *	Popup shell of the dialog
 *
 * Note:
 *	The function argument is passed the following arguments:
 *	form   - from widget that is the dialog
 *	ptr    - initial string for the dialog's text widget
 *	parent - parent of the dialog - the main text widget
 */
static Widget
CreateDialog(Widget parent, String ptr, String name, AddFunc func)
{
    Widget popup, form;
    Arg args[5];
    Cardinal num_args;

    num_args = 0;
    XtSetArg(args[num_args], XtNiconName, name);		num_args++;
    XtSetArg(args[num_args], XtNgeometry, NULL);		num_args++;
    XtSetArg(args[num_args], XtNallowShellResize, True);	num_args++;
    XtSetArg(args[num_args], XtNtransientFor, GetShell(parent));num_args++;
    popup = XtCreatePopupShell(name, transientShellWidgetClass,
			       parent, args, num_args);

    form = XtCreateManagedWidget(FORM_NAME, formWidgetClass, popup, NULL, 0);
    XtManageChild (form);

    (*func)(form, ptr, parent);

    return (popup);
}

/*
 * Function
 *	GetShell
 *	nearest shell widget.
 *
 * Parameters:
 *	w - widget whose parent shell should be returned
 *
 * Returns:
 *	  The shell widget among the ancestors of w that is the
 *	fewest levels up in the widget hierarchy.
 *
 * Description:
 *	Walks up the widget hierarchy to find the topmost shell widget.
 */
static Widget
GetShell(Widget w)
{
    while (w != NULL && !XtIsShell(w))
	w = XtParent(w);

    return (w);
}

static Bool
InParams(String str, String *p, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; p++, i++)
	if (!XmuCompareISOLatin1(*p, str))
	    return (True);
    return (False);
}

static char *WM_DELETE_WINDOW = "WM_DELETE_WINDOW";

static void
WMProtocols(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    Atom wm_delete_window;
    Atom wm_protocols;

    wm_delete_window = XInternAtom(XtDisplay(w), WM_DELETE_WINDOW, True);
    wm_protocols = XInternAtom(XtDisplay(w), "WM_PROTOCOLS", True);

    /* Respond to a recognized WM protocol request if
     * event type is ClientMessage and no parameters are passed, or
     * event type is ClientMessage and event data is matched to parameters, or
     * event type isn't ClientMessage and parameters make a request
     */
#define DO_DELETE_WINDOW InParams(WM_DELETE_WINDOW, params, *num_params)

    if ((event->type == ClientMessage
	 && event->xclient.message_type == wm_protocols
	 && event->xclient.data.l[0] == wm_delete_window
	 && (*num_params == 0 || DO_DELETE_WINDOW))
	|| (event->type != ClientMessage && DO_DELETE_WINDOW)) {
#undef DO_DELETE_WINDOW
	Widget cancel;
	char descendant[DISMISS_NAME_LEN + 2];

	snprintf(descendant, sizeof(descendant), "*%s", DISMISS_NAME);
	cancel = XtNameToWidget(w, descendant);
	if (cancel)
	    XtCallCallbacks(cancel, XtNcallback, NULL);
    }
}

static void
SetWMProtocolTranslations(Widget w)
{
    static XtTranslations compiled_table;
    static XtAppContext *app_context_list;
    static Cardinal list_size;

    unsigned int i;
    XtAppContext app_context;
    Atom wm_delete_window;

    app_context = XtWidgetToApplicationContext(w);

    /* parse translation table once */
    if (!compiled_table)
	compiled_table =
	    XtParseTranslationTable("<Message>WM_PROTOCOLS:XawWMProtocols()\n");

    /* add actions once per application context */
    for (i = 0; i < list_size && app_context_list[i] != app_context; i++)
	;
    if (i == list_size) {
	XtActionsRec actions[1];

	actions[0].string = "XawWMProtocols";
	actions[0].proc = WMProtocols;
	list_size++;
	app_context_list = (XtAppContext *)XtRealloc
	    ((char *)app_context_list, list_size * sizeof(XtAppContext));
	XtAppAddActions(app_context, actions, 1);
	app_context_list[i] = app_context;
    }

    /* establish communication between the window manager and each shell */
    XtAugmentTranslations(w, compiled_table);
    wm_delete_window = XInternAtom(XtDisplay(w), WM_DELETE_WINDOW, False);
    (void)XSetWMProtocols(XtDisplay(w), XtWindow(w), &wm_delete_window, 1);
}
