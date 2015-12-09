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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xos.h>		/* for select() and struct timeval */
#include <ctype.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Xfuncs.h>
#include <X11/Xutil.h>
#include <X11/Xmu/Atoms.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xmu/StdSel.h>
#include <X11/Xaw/MultiSinkP.h>
#include <X11/Xaw/MultiSrcP.h>
#include <X11/Xaw/TextP.h>
#include <X11/Xaw/TextSrcP.h>
#include <X11/Xaw/XawImP.h>
#include "Private.h"
#include "XawI18n.h"

#ifdef _WIN32
#include <X11/Xwinsock.h>
#endif

#define SrcScan			XawTextSourceScan
#define FindDist		XawTextSinkFindDistance
#define FindPos			XawTextSinkFindPosition
#define MULT(w)			(w->text.mult == 0 ? 4 :		\
				 w->text.mult == 32767 ? -4 : w->text.mult)

#define KILL_RING_APPEND	2
#define KILL_RING_BEGIN		3
#define KILL_RING_YANK		100
#define KILL_RING_YANK_DONE	98

#define XawTextActionMaxHexChars	100

/*
 * Prototypes
 */
static void _DeleteOrKill(TextWidget, XawTextPosition, XawTextPosition, Bool);
static void _SelectionReceived(Widget, XtPointer, Atom*, Atom*, XtPointer,
			       unsigned long*, int*);
static void _LoseSelection(Widget, Atom*, char**, int*);
static void AutoFill(TextWidget);
static Boolean ConvertSelection(Widget, Atom*, Atom*, Atom*, XtPointer*,
				unsigned long*, int*);
static void DeleteOrKill(TextWidget, XEvent*, XawTextScanDirection,
			 XawTextScanType, Bool, Bool);
static void EndAction(TextWidget);
#ifndef OLDXAW
static Bool BlankLine(Widget, XawTextPosition, int*);
static int DoFormatText(TextWidget, XawTextPosition, Bool, int,
			XawTextBlock*, XawTextPosition*, int, Bool);
static int FormatText(TextWidget, XawTextPosition, Bool,
		      XawTextPosition*, int);
static Bool GetBlockBoundaries(TextWidget, XawTextPosition*, XawTextPosition*);
#endif
static int FormRegion(TextWidget, XawTextPosition, XawTextPosition,
		      XawTextPosition*, int);
static void GetSelection(Widget, Time, String*, Cardinal);
static char *IfHexConvertHexElseReturnParam(char*, int*);
static void InsertNewCRs(TextWidget, XawTextPosition, XawTextPosition,
			 XawTextPosition*, int);
static int InsertNewLineAndBackupInternal(TextWidget);
static int LocalInsertNewLine(TextWidget, XEvent*);
static void LoseSelection(Widget, Atom*);
static void ParameterError(Widget, String);
static Bool MatchSelection(Atom, XawTextSelection*);
static void ModifySelection(TextWidget, XEvent*, XawTextSelectionMode,
			    XawTextSelectionAction, String*, Cardinal*);
static void Move(TextWidget, XEvent*, XawTextScanDirection, XawTextScanType,
		 Bool);
static void NotePosition(TextWidget, XEvent*);
static void StartAction(TextWidget, XEvent*);
static XawTextPosition StripOutOldCRs(TextWidget, XawTextPosition,
				      XawTextPosition, XawTextPosition*, int);
#ifndef OLDXAW
static Bool StripSpaces(TextWidget, XawTextPosition, XawTextPosition,
			XawTextPosition*, int, XawTextBlock*);
static Bool Tabify(TextWidget, XawTextPosition, XawTextPosition,
		   XawTextPosition*, int, XawTextBlock*);
static Bool Untabify(TextWidget, XawTextPosition, XawTextPosition,
		     XawTextPosition*, int, XawTextBlock*);
#endif

/*
 * Actions
 */
static void CapitalizeWord(Widget, XEvent*, String*, Cardinal*);
static void DisplayCaret(Widget, XEvent*, String*, Cardinal*);
static void Delete(Widget, XEvent*, String*, Cardinal*);
static void DeleteBackwardChar(Widget, XEvent*, String*, Cardinal*);
static void DeleteBackwardWord(Widget, XEvent*, String*, Cardinal*);
static void DeleteCurrentSelection(Widget, XEvent*, String*, Cardinal*);
static void DeleteForwardChar(Widget, XEvent*, String*, Cardinal*);
static void DeleteForwardWord(Widget, XEvent*, String*, Cardinal*);
static void DowncaseWord(Widget, XEvent*, String*, Cardinal*);
static void ExtendAdjust(Widget, XEvent*, String*, Cardinal*);
static void ExtendEnd(Widget, XEvent*, String*, Cardinal*);
static void ExtendStart(Widget, XEvent*, String*, Cardinal*);
static void FormParagraph(Widget, XEvent*, String*, Cardinal*);
#ifndef OLDXAW
static void Indent(Widget, XEvent*, String*, Cardinal*);
#endif
static void InsertChar(Widget, XEvent*, String*, Cardinal*);
static void InsertNewLine(Widget, XEvent*, String*, Cardinal*);
static void InsertNewLineAndBackup(Widget, XEvent*, String*, Cardinal*);
static void InsertNewLineAndIndent(Widget, XEvent*, String*, Cardinal*);
static void InsertSelection(Widget, XEvent*, String*, Cardinal*);
static void InsertString(Widget, XEvent*, String*, Cardinal*);
#ifndef OLDXAW
static void KeyboardReset(Widget, XEvent*, String*, Cardinal*);
#endif
static void KillBackwardWord(Widget, XEvent*, String*, Cardinal*);
static void KillCurrentSelection(Widget, XEvent*, String*, Cardinal*);
static void KillForwardWord(Widget, XEvent*, String*, Cardinal*);
#ifndef OLDXAW
static void KillRingYank(Widget, XEvent*, String*, Cardinal*);
#endif
static void KillToEndOfLine(Widget, XEvent*, String*, Cardinal*);
static void KillToEndOfParagraph(Widget, XEvent*, String*, Cardinal*);
static void MoveBackwardChar(Widget, XEvent*, String*, Cardinal*);
static void MoveBackwardWord(Widget, XEvent*, String*, Cardinal*);
static void MoveBackwardParagraph(Widget, XEvent*, String*, Cardinal*);
static void MoveBeginningOfFile(Widget, XEvent*, String*, Cardinal*);
static void MoveEndOfFile(Widget, XEvent*, String*, Cardinal*);
static void MoveForwardChar(Widget, XEvent*, String*, Cardinal*);
static void MoveForwardWord(Widget, XEvent*, String*, Cardinal*);
static void MoveForwardParagraph(Widget, XEvent*, String*, Cardinal*);
static void MoveNextLine(Widget, XEvent*, String*, Cardinal*);
static void MoveNextPage(Widget, XEvent*, String*, Cardinal*);
static void MovePage(TextWidget, XEvent*, XawTextScanDirection);
static void MovePreviousLine(Widget, XEvent*, String*, Cardinal*);
static void MovePreviousPage(Widget, XEvent*, String*, Cardinal*);
static void MoveLine(TextWidget, XEvent*, XawTextScanDirection);
static void MoveToLineEnd(Widget, XEvent*, String*, Cardinal*);
static void MoveToLineStart(Widget, XEvent*, String*, Cardinal*);
static void Multiply(Widget, XEvent*, String*, Cardinal*);
static void NoOp(Widget, XEvent*, String*, Cardinal*);
#ifndef OLDXAW
static void Numeric(Widget, XEvent*, String*, Cardinal*);
#endif
static void Reconnect(Widget, XEvent*, String*, Cardinal*);
static void RedrawDisplay(Widget, XEvent*, String*, Cardinal*);
static void Scroll(TextWidget, XEvent*, XawTextScanDirection);
static void ScrollOneLineDown(Widget, XEvent*, String*, Cardinal*);
static void ScrollOneLineUp(Widget, XEvent*, String*, Cardinal*);
static void SelectAdjust(Widget, XEvent*, String*, Cardinal*);
static void SelectAll(Widget, XEvent*, String*, Cardinal*);
static void SelectEnd(Widget, XEvent*, String*, Cardinal*);
static void SelectSave(Widget, XEvent*, String*, Cardinal*);
static void SelectStart(Widget, XEvent*, String*, Cardinal*);
static void SelectWord(Widget, XEvent*, String*, Cardinal*);
static void SetKeyboardFocus(Widget, XEvent*, String*, Cardinal*);
static void TextEnterWindow(Widget, XEvent*, String*, Cardinal*);
static void TextFocusIn(Widget, XEvent*, String*, Cardinal*);
static void TextFocusOut(Widget, XEvent*, String*, Cardinal*);
static void TextLeaveWindow(Widget, XEvent*, String*, Cardinal*);
static void TransposeCharacters(Widget, XEvent*, String*, Cardinal*);
#ifndef OLDXAW
static void ToggleOverwrite(Widget, XEvent*, String*, Cardinal*);
static void Undo(Widget, XEvent*, String*, Cardinal*);
#endif
static void UpcaseWord(Widget, XEvent*, String*, Cardinal*);
static void DestroyFocusCallback(Widget, XtPointer, XtPointer);

/*
 * External
 */
void _XawTextZapSelection(TextWidget, XEvent*, Bool);

/*
 * Defined in TextPop.c
 */
void _XawTextInsertFileAction(Widget, XEvent*, String*, Cardinal*);
void _XawTextInsertFile(Widget, XEvent*, String*, Cardinal*);
void _XawTextSearch(Widget, XEvent*, String*, Cardinal*);
void _XawTextDoSearchAction(Widget, XEvent*, String*, Cardinal*);
void _XawTextDoReplaceAction(Widget, XEvent*, String*, Cardinal*);
void _XawTextSetField(Widget, XEvent*, String*, Cardinal*);
void _XawTextPopdownSearchAction(Widget, XEvent*, String*, Cardinal*);

/*
 * These are defined in Text.c
 */
void _XawTextAlterSelection(TextWidget, XawTextSelectionMode,
			    XawTextSelectionAction, String*, Cardinal*);
void _XawTextClearAndCenterDisplay(TextWidget);
void _XawTextExecuteUpdate(TextWidget);
char *_XawTextGetText(TextWidget, XawTextPosition, XawTextPosition);
void _XawTextPrepareToUpdate(TextWidget);
int _XawTextReplace(TextWidget, XawTextPosition, XawTextPosition,
			   XawTextBlock*);
Atom *_XawTextSelectionList(TextWidget, String*, Cardinal);
void _XawTextSetSelection(TextWidget, XawTextPosition, XawTextPosition,
				 String*, Cardinal);
void _XawTextVScroll(TextWidget, int);
void XawTextScroll(TextWidget, int, int);
void _XawTextSetLineAndColumnNumber(TextWidget, Bool);

#ifndef OLDXAW
/*
 * Defined in TextSrc.c
 */
Bool _XawTextSrcUndo(TextSrcObject, XawTextPosition*);
Bool _XawTextSrcToggleUndo(TextSrcObject);
void _XawSourceSetUndoErase(TextSrcObject, int);
void _XawSourceSetUndoMerge(TextSrcObject, Bool);
#endif /* OLDXAW */

/*
 * Initialization
 */
#ifndef OLDXAW
#define MAX_KILL_RINGS	1024
XawTextKillRing *xaw_text_kill_ring;
static XawTextKillRing kill_ring_prev, kill_ring_null = { &kill_ring_prev, };
static unsigned num_kill_rings;
#endif

/*
 * Implementation
 */
static void
ParameterError(Widget w, String param)
{
    String params[2];
    Cardinal num_params = 2;
    params[0] = XtName(w);
    params[1] = param;

    XtAppWarningMsg(XtWidgetToApplicationContext(w),
		    "parameterError", "textAction", "XawError",
		    "Widget: %s Parameter: %s",
		    params, &num_params);
    XBell(XtDisplay(w), 50);
}

static void
StartAction(TextWidget ctx, XEvent *event)
{
#ifndef OLDXAW
    Cardinal i;
    TextSrcObject src = (TextSrcObject)ctx->text.source;

    for (i = 0; i < src->textSrc.num_text; i++)
	_XawTextPrepareToUpdate((TextWidget)src->textSrc.text[i]);
    _XawSourceSetUndoMerge(src, False);
#else
    _XawTextPrepareToUpdate(ctx);
#endif

    if (event != NULL) {
	switch (event->type) {
	    case ButtonPress:
	    case ButtonRelease:
		ctx->text.time = event->xbutton.time;
		break;
	    case KeyPress:
	    case KeyRelease:
		ctx->text.time = event->xkey.time;
		break;
	    case MotionNotify:
		ctx->text.time = event->xmotion.time;
		break;
	    case EnterNotify:
	    case LeaveNotify:
		ctx->text.time = event->xcrossing.time;
	}
    }
}

static void
NotePosition(TextWidget ctx, XEvent *event)
{
    switch (event->type) {
	case ButtonPress:
	case ButtonRelease:
	    ctx->text.ev_x = event->xbutton.x;
	    ctx->text.ev_y = event->xbutton.y;
	    break;
	case KeyPress:
	case KeyRelease: {
	    XRectangle cursor;
	    XawTextSinkGetCursorBounds(ctx->text.sink, &cursor);
	    ctx->text.ev_x = cursor.x + cursor.width / 2;
	    ctx->text.ev_y = cursor.y + cursor.height / 2;
	}   break;
	case MotionNotify:
	    ctx->text.ev_x = event->xmotion.x;
	    ctx->text.ev_y = event->xmotion.y;
	    break;
	case EnterNotify:
	case LeaveNotify:
	    ctx->text.ev_x = event->xcrossing.x;
	    ctx->text.ev_y = event->xcrossing.y;
    }
}

static void
EndAction(TextWidget ctx)
{
#ifndef OLDXAW
    Cardinal i;
    TextSrcObject src = (TextSrcObject)ctx->text.source;

    for (i = 0; i < src->textSrc.num_text; i++)
	_XawTextExecuteUpdate((TextWidget)src->textSrc.text[i]);

    ctx->text.mult = 1;
    ctx->text.numeric = False;
    if (ctx->text.kill_ring) {
	if (--ctx->text.kill_ring == KILL_RING_YANK_DONE) {
	    if (ctx->text.kill_ring_ptr) {
		--ctx->text.kill_ring_ptr->refcount;
		ctx->text.kill_ring_ptr = NULL;
	    }
	}
    }
#else
    ctx->text.mult = 1;
    _XawTextExecuteUpdate(ctx);
#endif /* OLDXAW */
}

struct _SelectionList {
    String* params;
    Cardinal count;
    Time time;
    int asked;		/* which selection currently has been asked for:
			   0 = UTF8_STRING, 1 = COMPOUND_TEXT, 2 = STRING */
    Atom selection;	/* selection atom (normally XA_PRIMARY) */
};

/*ARGSUSED*/
static void
_SelectionReceived(Widget w, XtPointer client_data, Atom *selection,
		   Atom *type, XtPointer value, unsigned long *length,
		   int *format)
{
    Display *d = XtDisplay(w);
    TextWidget ctx = (TextWidget)w;
    XawTextBlock text;

    if (*type == 0 /*XT_CONVERT_FAIL*/ || *length == 0) {
	struct _SelectionList* list = (struct _SelectionList*)client_data;

	if (list != NULL) {
	    if (list->asked == 0) {
		/* If we just asked for XA_UTF8_STRING and got no response,
		   we'll ask again, this time for XA_COMPOUND_TEXT. */
		list->asked++;
		XtGetSelectionValue(w, list->selection, XA_COMPOUND_TEXT(d),
				    _SelectionReceived,
				    (XtPointer)list, list->time);
	    } else if (list->asked == 1) {
		/* If we just asked for XA_COMPOUND_TEXT and got no response,
		   we'll ask again, this time for XA_STRING. */
		list->asked++;
		XtGetSelectionValue(w, list->selection, XA_STRING,
				    _SelectionReceived,
				    (XtPointer)list, list->time);
	    } else {
		/* We tried all possible text targets in this param.
		   Recurse on the tail of the params list. */
		GetSelection(w, list->time, list->params, list->count);
		XtFree(client_data);
	    }
	}
	return;
    }

    StartAction(ctx, NULL);
    if (XawTextFormat(ctx, XawFmtWide)) {
	XTextProperty textprop;
	wchar_t **wlist;
	int count;

	textprop.encoding = *type;
	textprop.value = (unsigned char *)value;
	textprop.nitems = strlen(value);
	textprop.format = 8;

	if (XwcTextPropertyToTextList(d, &textprop, &wlist, &count)
	    !=	Success
	    || count < 1) {
	    XwcFreeStringList(wlist);

	    /* Notify the user on strerr and in the insertion :) */
	    fprintf(stderr, "Xaw Text Widget: An attempt was made to insert "
		    "an illegal selection.\n");

	    textprop.value = (unsigned char *)" >> ILLEGAL SELECTION << ";
	    textprop.nitems = strlen((char *) textprop.value);
	    if (XwcTextPropertyToTextList(d, &textprop, &wlist, &count)
		!=  Success
		|| count < 1)
		return;
	}

	XFree(value);
	value = (XPointer)wlist[0];

	*length = wcslen(wlist[0]);
	XtFree((XtPointer)wlist);
	text.format = XawFmtWide;
    }
    text.ptr = (char*)value;
    text.firstPos = 0;
    text.length = *length;
    if (_XawTextReplace(ctx, ctx->text.insertPos, ctx->text.insertPos, &text)) {
	XBell(XtDisplay(ctx), 0);
	EndAction(ctx);
	return;
    }

    ctx->text.from_left = -1;
    ctx->text.insertPos = SrcScan(ctx->text.source, ctx->text.old_insert,
				  XawstPositions, XawsdRight, text.length, True);

    EndAction(ctx);
    XtFree(client_data);
    XFree(value);	/* the selection value should be freed with XFree */
}

static void
GetSelection(Widget w, Time timev, String *params, Cardinal num_params)
{
    Display *d = XtDisplay(w);
    TextWidget ctx = (TextWidget)w;
    Atom selection;
    int buffer;

    selection = XInternAtom(XtDisplay(w), *params, False);
    switch (selection) {
	case XA_CUT_BUFFER0: buffer = 0; break;
	case XA_CUT_BUFFER1: buffer = 1; break;
	case XA_CUT_BUFFER2: buffer = 2; break;
	case XA_CUT_BUFFER3: buffer = 3; break;
	case XA_CUT_BUFFER4: buffer = 4; break;
	case XA_CUT_BUFFER5: buffer = 5; break;
	case XA_CUT_BUFFER6: buffer = 6; break;
	case XA_CUT_BUFFER7: buffer = 7; break;
	default:	     buffer = -1;
    }
    if (buffer >= 0) {
	int nbytes;
	unsigned long length;
	int fmt8 = 8;
	Atom type = XA_STRING;
	char *line = XFetchBuffer(XtDisplay(w), &nbytes, buffer);

	if ((length = nbytes) != 0L)
	    _SelectionReceived(w, NULL, &selection, &type, line, &length, &fmt8);
	else if (num_params > 1)
	    GetSelection(w, timev, params+1, num_params-1);
    }
    else {
	struct _SelectionList* list;

	if (--num_params) {
	    list = XtNew(struct _SelectionList);
	    list->params = params + 1;
	    list->count = num_params;
	    list->time = timev;
	    list->asked = 0;
	    list->selection = selection;
	}
	else
	    list = NULL;
	XtGetSelectionValue(w, selection, XawTextFormat(ctx, XawFmtWide) ?
			    XA_UTF8_STRING(d) : XA_TEXT(d),
			    _SelectionReceived, (XtPointer)list, timev);
    }
}

static void
InsertSelection(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    StartAction((TextWidget)w, event);	/* Get Time. */
    GetSelection(w, ((TextWidget)w)->text.time, params, *num_params);
    EndAction((TextWidget)w);
}

/*
 * Routines for Moving Around
 */
static void
Move(TextWidget ctx, XEvent *event, XawTextScanDirection dir,
     XawTextScanType type, Bool include)
{
    XawTextPosition insertPos;
    short mult = MULT(ctx);

    if (mult < 0) {
	mult = -mult;
	dir = dir == XawsdLeft ? XawsdRight : XawsdLeft;
    }

    insertPos = SrcScan(ctx->text.source, ctx->text.insertPos,
			type, dir, mult, include);

    StartAction(ctx, event);

    if (ctx->text.s.left != ctx->text.s.right)
	XawTextUnsetSelection((Widget)ctx);

#ifndef OLDXAW
    ctx->text.numeric = False;
#endif
    ctx->text.mult = 1;
    ctx->text.showposition = True;
    ctx->text.from_left = -1;
    ctx->text.insertPos = insertPos;
    EndAction(ctx);
}

/*ARGSUSED*/
static void
MoveForwardChar(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Move((TextWidget)w, event, XawsdRight, XawstPositions, True);
}

/*ARGSUSED*/
static void
MoveBackwardChar(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Move((TextWidget)w, event, XawsdLeft, XawstPositions, True);
}

static void
MoveForwardWord(Widget w, XEvent *event, String *p, Cardinal *n)
{
    if (*n && (p[0][0] == 'A' || p[0][0] == 'a'))
	Move((TextWidget)w, event, XawsdRight, XawstAlphaNumeric, False);
    else
	Move((TextWidget)w, event, XawsdRight, XawstWhiteSpace, False);
}

static void
MoveBackwardWord(Widget w, XEvent *event, String *p, Cardinal *n)
{
    if (*n && (p[0][0] == 'A' || p[0][0] == 'a'))
	Move((TextWidget)w, event, XawsdLeft, XawstAlphaNumeric, False);
    else
	Move((TextWidget)w, event, XawsdLeft, XawstWhiteSpace, False);
}

static void
MoveForwardParagraph(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition position = ctx->text.insertPos;
    short mult = MULT(ctx);

    if (mult < 0) {
	ctx->text.mult = -mult;
	MoveBackwardParagraph(w, event, p, n);
	return;
    }

    while (mult--) {
	position = SrcScan(ctx->text.source, position,
			   XawstEOL, XawsdRight, 1, False) - 1;

	while (position == SrcScan(ctx->text.source, position,
				   XawstEOL, XawsdRight, 1, False))
	    if (++position > ctx->text.lastPos) {
		mult = 0;
		break;
	    }

	position = SrcScan(ctx->text.source, position,
			   XawstParagraph, XawsdRight, 1, True);
	if (position != ctx->text.lastPos)
	    position = SrcScan(ctx->text.source, position - 1,
			       XawstEOL, XawsdLeft, 1, False);
	else
	    break;
    }

    if (position != ctx->text.insertPos) {
	XawTextUnsetSelection(w);
	StartAction(ctx, event);
	ctx->text.showposition = True;
	ctx->text.from_left = -1;
	ctx->text.insertPos = position;
	EndAction(ctx);
    }
    else
	ctx->text.mult = 1;
}

/*ARGSUSED*/
static void
MoveBackwardParagraph(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition position = ctx->text.insertPos;
    short mult = MULT(ctx);

    if (mult < 0) {
	ctx->text.mult = -mult;
	MoveForwardParagraph(w, event, p, n);
	return;
    }

    while (mult--) {
	position = SrcScan(ctx->text.source, position,
			   XawstEOL, XawsdLeft, 1, False) + 1;

	while (position == SrcScan(ctx->text.source, position,
				   XawstEOL, XawsdLeft, 1, False))
	    if (--position < 0) {
		mult = 0;
		break;
	    }

	position = SrcScan(ctx->text.source, position,
			   XawstParagraph, XawsdLeft, 1, True);
	if (position > 0 && position < ctx->text.lastPos)
	    ++position;
	else
	    break;
    }

    if (position != ctx->text.insertPos) {
	XawTextUnsetSelection(w);
	StartAction(ctx, event);
	ctx->text.showposition = True;
	ctx->text.from_left = -1;
	ctx->text.insertPos = position;
	EndAction(ctx);
    }
    else
	ctx->text.mult = 1;
}

/*ARGSUSED*/
static void
MoveToLineEnd(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Move((TextWidget)w, event, XawsdRight, XawstEOL, False);
}

/*ARGSUSED*/
static void
MoveToLineStart(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Move((TextWidget)w, event, XawsdLeft, XawstEOL, False);
}

static void
MoveLine(TextWidget ctx, XEvent *event, XawTextScanDirection dir)
{
    XawTextPosition cnew, next_line, ltemp;
    int itemp, from_left;
    short mult = MULT(ctx);

    StartAction(ctx, event);

    XawTextUnsetSelection((Widget)ctx);

    if (dir == XawsdLeft)
	mult = mult == 0 ? 5 : mult + 1;

    cnew = SrcScan(ctx->text.source, ctx->text.insertPos,
		   XawstEOL, XawsdLeft, 1, False);

    if (ctx->text.from_left < 0)
	FindDist(ctx->text.sink, cnew, ctx->text.left_margin, ctx->text.insertPos,
		 &ctx->text.from_left, &ltemp, &itemp);

    cnew = SrcScan(ctx->text.source, ctx->text.insertPos, XawstEOL, dir,
		   mult, (dir == XawsdRight));

    next_line = SrcScan(ctx->text.source, cnew, XawstEOL, XawsdRight, 1, False);

    FindPos(ctx->text.sink, cnew, ctx->text.left_margin, ctx->text.from_left,
	    False, &ctx->text.insertPos, &from_left, &itemp);

    if (from_left < ctx->text.from_left) {
	XawTextBlock block;

	XawTextSourceRead(ctx->text.source, ctx->text.insertPos, &block, 1);
	if (block.length) {
	    if (XawTextFormat(ctx, XawFmtWide)) {
		if (*(wchar_t *)block.ptr == _Xaw_atowc(XawTAB))
		    ++ctx->text.insertPos;
	    }
	    else if (block.ptr[0] == XawTAB)
		++ctx->text.insertPos;
	}
    }

    if (ctx->text.insertPos > next_line)
	ctx->text.insertPos = next_line;

    EndAction(ctx);
}

static void
MoveNextLine(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    short mult = MULT(ctx);

    if (mult < 0) {
	ctx->text.mult = -mult;
	MovePreviousLine(w, event, p, n);
	return;
    }

    if (ctx->text.insertPos < ctx->text.lastPos)
	MoveLine(ctx, event, XawsdRight);
    else
	ctx->text.mult = 1;
}

static void
MovePreviousLine(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    short mult = MULT(ctx);

    if (mult < 0) {
	ctx->text.mult = -mult;
	MoveNextLine(w, event, p, n);
	return;
    }

    if (ctx->text.lt.top != 0 || (ctx->text.lt.lines > 1 &&
	ctx->text.insertPos >= ctx->text.lt.info[1].position))
	MoveLine(ctx, event, XawsdLeft);
    else
	ctx->text.mult = 1;
}

/*ARGSUSED*/
static void
MoveBeginningOfFile(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Move((TextWidget)w, event, XawsdLeft, XawstAll, True);
}

/*ARGSUSED*/
static void
MoveEndOfFile(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Move((TextWidget)w, event, XawsdRight, XawstAll, True);
}

static void
Scroll(TextWidget ctx, XEvent *event, XawTextScanDirection dir)
{
    short mult = MULT(ctx);

    if (mult < 0) {
	mult = -mult;
	dir = dir == XawsdLeft ? XawsdRight : XawsdLeft;
    }

    if (ctx->text.lt.lines > 1
	&& (dir == XawsdRight
	    || ctx->text.lastPos >= ctx->text.lt.info[1].position)) {
	StartAction(ctx, event);

	if (dir == XawsdLeft)
	    _XawTextVScroll(ctx, mult);
	else
	    _XawTextVScroll(ctx, -mult);

	EndAction(ctx);
    }
    else {
	ctx->text.mult = 1;
#ifndef OLDXAW
	ctx->text.numeric = False;
#endif
    }
}

/*ARGSUSED*/
static void
ScrollOneLineUp(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Scroll((TextWidget)w, event, XawsdLeft);
}

/*ARGSUSED*/
static void
ScrollOneLineDown(Widget w, XEvent *event, String *p, Cardinal *n)
{
    Scroll((TextWidget)w, event, XawsdRight);
}

static void
MovePage(TextWidget ctx, XEvent *event, XawTextScanDirection dir)
{
    int scroll_val = 0;
    XawTextPosition old_pos;

    ctx->text.from_left = -1;
    switch (dir) {
	case XawsdLeft:
	    if (ctx->text.lt.top != 0)
		scroll_val = -Max(1, ctx->text.lt.lines - 1);
		break;
	case XawsdRight:
	    if (!IsPositionVisible(ctx, Max(0, ctx->text.lastPos)))
		scroll_val = Max(1, ctx->text.lt.lines - 1);
	    break;
    }

    if (scroll_val)
	XawTextScroll(ctx, scroll_val,
		      ctx->text.left_margin - ctx->text.r_margin.left);

    old_pos = ctx->text.insertPos;
    switch (dir) {
	case XawsdRight:
	    if (IsPositionVisible(ctx, Max(0, ctx->text.lastPos)))
		ctx->text.insertPos = Max(0, ctx->text.lastPos);
	    else
		ctx->text.insertPos = ctx->text.lt.top;
	    if (ctx->text.insertPos < old_pos)
		ctx->text.insertPos = SrcScan(ctx->text.source, old_pos,
					      XawstEOL, XawsdLeft, 1, False);
	    break;
	case XawsdLeft:
	    if (IsPositionVisible(ctx, 0))
		ctx->text.insertPos = 0;
	    else if (ctx->text.lt.lines)
		ctx->text.insertPos =
		    ctx->text.lt.info[ctx->text.lt.lines - 1].position;
	    else
		ctx->text.insertPos = ctx->text.lt.top;
	    if (ctx->text.insertPos > old_pos)
		ctx->text.insertPos = SrcScan(ctx->text.source, old_pos,
					      XawstEOL, XawsdLeft, 1, False);
	    break;
    }
}

static void
MoveNextPage(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    short mult = MULT(ctx);

    if (mult < 0) {
	ctx->text.mult = -mult;
	MovePreviousPage(w, event, p, n);
	return;
    }

    if (ctx->text.insertPos < ctx->text.lastPos) {
	XawTextUnsetSelection(w);
	StartAction(ctx, event);
	ctx->text.clear_to_eol = True;
	while (mult-- && ctx->text.insertPos < ctx->text.lastPos)
	    MovePage(ctx, event, XawsdRight);
	EndAction(ctx);
    }
    else
	ctx->text.mult = 1;
}

/*ARGSUSED*/
static void
MovePreviousPage(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    short mult = MULT(ctx);

    if (mult < 0) {
	ctx->text.mult = -mult;
	MoveNextPage(w, event, p, n);
	return;
    }

    if (ctx->text.insertPos > 0) {
	XawTextUnsetSelection(w);
	StartAction(ctx, event);
	ctx->text.clear_to_eol = True;
	while (mult-- && ctx->text.insertPos > 0)
	    MovePage(ctx, event, XawsdLeft);
	EndAction(ctx);
    }
    else
	ctx->text.mult = 1;
}

/*
 * Delete Routines
 */
static Bool
MatchSelection(Atom selection, XawTextSelection *s)
{
    Atom *match;
    int count;

    for (count = 0, match = s->selections; count < s->atom_count;
	 match++, count++)
	if (*match == selection)
	    return (True);

    return (False);
}

#define SrcCvtSel	XawTextSourceConvertSelection

static Boolean
ConvertSelection(Widget w, Atom *selection, Atom *target, Atom *type,
		 XtPointer *value, unsigned long *length, int *format)
{
    Display *d = XtDisplay(w);
    TextWidget ctx = (TextWidget)w;
    Widget src = ctx->text.source;
    XawTextEditType edit_mode;
    Arg args[1];
    XawTextSelectionSalt *salt = NULL;
    XawTextSelection *s;

    if (*target == XA_TARGETS(d)) {
	Atom *targetP, *std_targets;
	unsigned long std_length;

	if (SrcCvtSel(src, selection, target, type, value, length, format))
	    return (True);

	XtSetArg(args[0], XtNeditType,&edit_mode);
	XtGetValues(src, args, 1);

	XmuConvertStandardSelection(w, ctx->text.time, selection,
				    target, type, (XPointer *)&std_targets,
				    &std_length, format);

	*length = 7 + (edit_mode == XawtextEdit) + std_length;
	*value = XtMalloc((unsigned)sizeof(Atom)*(*length));
	targetP = *(Atom**)value;
	*targetP++ = XA_STRING;
	*targetP++ = XA_TEXT(d);
	*targetP++ = XA_UTF8_STRING(d);
	*targetP++ = XA_COMPOUND_TEXT(d);
	*targetP++ = XA_LENGTH(d);
	*targetP++ = XA_LIST_LENGTH(d);
	*targetP++ = XA_CHARACTER_POSITION(d);
	if (edit_mode == XawtextEdit) {
	    *targetP++ = XA_DELETE(d);
	}
	memcpy((char*)targetP, (char*)std_targets, sizeof(Atom)*std_length);
	XtFree((char*)std_targets);
	*type = XA_ATOM;
	*format = 32;
	return (True);
    }

    if (SrcCvtSel(src, selection, target, type, value, length, format))
	return (True);

    for (salt = ctx->text.salt2; salt; salt = salt->next)
	if (MatchSelection (*selection, &salt->s))
	    break;
    if (!salt)
	return (False);
    s = &salt->s;
    if (*target == XA_STRING
	|| *target == XA_TEXT(d)
	|| *target == XA_UTF8_STRING(d)
	|| *target == XA_COMPOUND_TEXT(d)) {
	if (*target == XA_TEXT(d)) {
	    if (XawTextFormat(ctx, XawFmtWide))
		*type = XA_COMPOUND_TEXT(d);
	    else
		*type = XA_STRING;
	}
	else
	  *type = *target;

	/*
	 * If salt is True, the salt->contents stores CT string,
	 * its length is measured in bytes.
	 * Refer to _XawTextSaltAwaySelection()
	 *
	 * by Li Yuhong, Mar. 20, 1991.
	 */
	if (!salt) {
	    *value = (char *)_XawTextGetSTRING(ctx, s->left, s->right);
	    if (XawTextFormat(ctx, XawFmtWide)) {
		XTextProperty textprop;
		if (XwcTextListToTextProperty(d, (wchar_t**)value, 1,
					      XCompoundTextStyle, &textprop)
		    < Success) {
		    XtFree(*value);
		    return (False);
		}
		XtFree(*value);
		*value = (XtPointer)textprop.value;
		*length = textprop.nitems;
	    }
	    else
		*length = strlen(*value);
	}
	else {
	    *value = XtMalloc((salt->length + 1) * sizeof(unsigned char));
	    strcpy (*value, salt->contents);
	    *length = salt->length;
	}
	/* Got *value,*length, now in COMPOUND_TEXT format. */
	if (XawTextFormat(ctx, XawFmtWide)) {
	    if (*type == XA_STRING) {
		XTextProperty textprop;
		wchar_t **wlist;
		int count;

		textprop.encoding = XA_COMPOUND_TEXT(d);
		textprop.value = (unsigned char *)*value;
		textprop.nitems = strlen(*value);
		textprop.format = 8;
		if (XwcTextPropertyToTextList(d, &textprop, &wlist, &count)
		     < Success
		    || count < 1) {
		    XtFree(*value);
		    return (False);
		}
		XtFree(*value);
		if (XwcTextListToTextProperty(d, wlist, 1, XStringStyle, &textprop)
		     < Success) {
		    XwcFreeStringList((wchar_t**)wlist);
		    return (False);
		}
		*value = (XtPointer)textprop.value;
		*length = textprop.nitems;
		XwcFreeStringList((wchar_t**) wlist);
	    }
	    else if (*type == XA_UTF8_STRING(d)) {
		XTextProperty textprop;
		char **list;
		int count;

		textprop.encoding = XA_COMPOUND_TEXT(d);
		textprop.value = (unsigned char *)*value;
		textprop.nitems = strlen(*value);
		textprop.format = 8;
		if (Xutf8TextPropertyToTextList(d, &textprop, &list, &count)
		    < Success
		    || count < 1) {
		    XtFree(*value);
		    return (False);
		}
		XtFree(*value);
		*value = *list;
		*length = strlen(*list);
		XFree(list);
	    }
	}
	*format = 8;
	return (True);
    }

    if (*target == XA_LIST_LENGTH(d) || *target == XA_LENGTH(d)) {
	long *temp;

	temp = (long *)XtMalloc(sizeof(long));
	if (*target == XA_LIST_LENGTH(d))
	    *temp = 1L;
	else			/* *target == XA_LENGTH(d) */
	    *temp = (long)(s->right - s->left);

	*value = (XPointer)temp;
	*type = XA_INTEGER;
	*length = 1L;
	*format = 32;
	return (True);
    }

    if (*target == XA_CHARACTER_POSITION(d)) {
	long *temp;

	temp = (long *) XtMalloc(2 * sizeof(long));
	temp[0] = (long)(s->left + 1);
	temp[1] = s->right;
	*value = (XPointer)temp;
	*type = XA_SPAN(d);
	*length = 2L;
	*format = 32;
	return (True);
    }

    if (*target == XA_DELETE(d)) {
	if (!salt)
	    _XawTextZapSelection(ctx, NULL, True);
	*value = NULL;
	*type = XA_NULL(d);
	*length = 0;
	*format = 32;
	return (True);
    }

    if (XmuConvertStandardSelection(w, ctx->text.time, selection, target, type,
				    (XPointer *)value, length, format))
	return (True);

    return (False);
}

static void
LoseSelection(Widget w, Atom *selection)
{
    _LoseSelection(w, selection, NULL, NULL);
}

static void
_LoseSelection(Widget w, Atom *selection, char **contents, int *length)
{
    TextWidget ctx = (TextWidget)w;
    Atom *atomP;
    int i;
    XawTextSelectionSalt *salt, *prevSalt, *nextSalt;

    prevSalt = 0;
    for (salt = ctx->text.salt2; salt; salt = nextSalt) {
	atomP = salt->s.selections;
	nextSalt = salt->next;
	for (i = 0 ; i < salt->s.atom_count; i++, atomP++)
	    if (*selection == *atomP)
		*atomP = (Atom)0;

	while (salt->s.atom_count
	       && salt->s.selections[salt->s.atom_count-1] == 0)
	    salt->s.atom_count--;

	/*
	 * Must walk the selection list in opposite order from UnsetSelection.
	 */
	atomP = salt->s.selections;
	for (i = 0 ; i < salt->s.atom_count; i++, atomP++)
	    if (*atomP == (Atom)0) {
		*atomP = salt->s.selections[--salt->s.atom_count];

		while (salt->s.atom_count
		       && salt->s.selections[salt->s.atom_count-1] == 0)
		    salt->s.atom_count--;
	    }
	if (salt->s.atom_count == 0) {
#ifndef OLDXAW
	    if (contents == NULL) {
		XawTextKillRing *kill_ring = XtNew(XawTextKillRing);

		kill_ring->next = xaw_text_kill_ring;
		kill_ring->contents = salt->contents;
		kill_ring->length = salt->length;
		kill_ring->format = XawFmt8Bit;
		xaw_text_kill_ring = kill_ring;
		kill_ring_prev.next = xaw_text_kill_ring;

		if (++num_kill_rings > MAX_KILL_RINGS) {
		    XawTextKillRing *tail = NULL;

		    while (kill_ring->next) {
			tail = kill_ring;
			kill_ring = kill_ring->next;
		    }
		    if (kill_ring->refcount == 0) {
			--num_kill_rings;
			tail->next = NULL;
			XtFree(kill_ring->contents);
			XtFree((char*)kill_ring);
		    }
		}
	    }
	    else {
		*contents = salt->contents;
		*length = salt->length;
	    }
#endif
	    if (prevSalt)
		prevSalt->next = nextSalt;
	    else
		ctx->text.salt2 = nextSalt;

	    XtFree((char *)salt->s.selections);
	    XtFree((char *)salt);
	}
	else
	    prevSalt = salt;
    }
}

static void
_DeleteOrKill(TextWidget ctx, XawTextPosition from, XawTextPosition to,
	      Bool kill)
{
    XawTextBlock text;

#ifndef OLDXAW
    if (ctx->text.kill_ring_ptr) {
	--ctx->text.kill_ring_ptr->refcount;
	ctx->text.kill_ring_ptr = NULL;
    }
#endif
    if (kill && from < to) {
#ifndef OLDXAW
	Bool append = False;
	char *ring = NULL;
	XawTextPosition old_from = from;
#endif
	char *string;
	int size = 0, length;
	XawTextSelectionSalt *salt;
	Atom selection = XInternAtom(XtDisplay(ctx), "SECONDARY", False);

#ifndef OLDXAW
	if (ctx->text.kill_ring == KILL_RING_APPEND) {
	    old_from = ctx->text.salt2->s.left;
	    append = True;
	}
	else
	    ctx->text.kill_ring = KILL_RING_BEGIN;

	if (append)
	    _LoseSelection((Widget)ctx, &selection, &ring, &size);
	else
#endif
	    LoseSelection((Widget)ctx, &selection);

	salt = (XawTextSelectionSalt*)XtMalloc(sizeof(XawTextSelectionSalt));
	salt->s.selections = (Atom *)XtMalloc(sizeof(Atom));
	salt->s.left = from;
	salt->s.right = to;

	string = (char *)_XawTextGetSTRING(ctx, from, to);

	if (XawTextFormat(ctx, XawFmtWide)) {
	    XTextProperty textprop;

	    if (XwcTextListToTextProperty(XtDisplay((Widget)ctx),
					  (wchar_t**)(&string),
					  1, XCompoundTextStyle,
					  &textprop) <  Success) {
		XtFree(string);
		XtFree((char*)salt->s.selections);
		XtFree((char*)salt);
		return;
	    }
	    XtFree(string);
	    string = (char *)textprop.value;
	    length = textprop.nitems;
	}
	else
	    length = strlen(string);

	salt->length = length + size;

#ifndef OLDXAW
	if (!append)
	    salt->contents = string;
	else {
	    salt->contents = XtMalloc(length + size + 1);
	    if (from >= old_from) {
		strncpy(salt->contents, ring, size);
		salt->contents[size] = '\0';
		strncat(salt->contents, string, length);
	    }
	    else {
		strncpy(salt->contents, string, length);
		salt->contents[length] = '\0';
		strncat(salt->contents, ring, size);
	    }
	    salt->contents[length + size] = '\0';
	    XtFree(ring);
	    XtFree(string);
	}

	kill_ring_prev.contents = salt->contents;
	kill_ring_prev.length = salt->length;
	kill_ring_prev.format = XawFmt8Bit;
#else
	salt->contents = string;
#endif

	salt->next = ctx->text.salt2;
	ctx->text.salt2 = salt;

#ifndef OLDXAW
	if (append)
	    ctx->text.kill_ring = KILL_RING_BEGIN;
#endif

	salt->s.selections[0] = selection;

	XtOwnSelection((Widget)ctx, selection, ctx->text.time,
		       ConvertSelection, LoseSelection, NULL);
	salt->s.atom_count = 1;
    }
    text.length = 0;
    text.firstPos = 0;

    text.format = _XawTextFormat(ctx);
    text.ptr = "";

    if (_XawTextReplace(ctx, from, to, &text)) {
	XBell(XtDisplay(ctx), 50);
	return;
    }
    ctx->text.from_left = -1;
    ctx->text.insertPos = from;
    ctx->text.showposition = TRUE;
}

static void
DeleteOrKill(TextWidget ctx, XEvent *event, XawTextScanDirection dir,
	     XawTextScanType type, Bool include, Bool kill)
{
    XawTextPosition from, to;
    short mult = MULT(ctx);

    if (mult < 0) {
	mult = -mult;
	dir = dir == XawsdLeft ? XawsdRight : XawsdLeft;
    }

    StartAction(ctx, event);
#ifndef OLDXAW
    if (mult == 1)
	_XawSourceSetUndoMerge((TextSrcObject)ctx->text.source, True);
#endif
    to = SrcScan(ctx->text.source, ctx->text.insertPos,
		 type, dir, mult, include);

    /*
     * If no movement actually happened, then bump the count and try again.
     * This causes the character position at the very beginning and end of
     * a boundary to act correctly
     */
    if (to == ctx->text.insertPos)
	to = SrcScan(ctx->text.source, ctx->text.insertPos,
		     type, dir, mult + 1, include);

    if (dir == XawsdLeft) {
	from = to;
	to = ctx->text.insertPos;
    }
    else
	from = ctx->text.insertPos;

    _DeleteOrKill(ctx, from, to, kill);
    EndAction(ctx);
}

static void
Delete(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;

    if (ctx->text.s.left != ctx->text.s.right)
	DeleteCurrentSelection(w, event, p, n);
    else
	DeleteBackwardChar(w, event, p, n);
}

static void
DeleteChar(Widget w, XEvent *event, XawTextScanDirection dir)
{
    TextWidget ctx = (TextWidget)w;
    short mul = MULT(ctx);

    if (mul < 0) {
	ctx->text.mult = mul = -mul;
	dir = dir == XawsdLeft ? XawsdRight : XawsdLeft;
    }
    DeleteOrKill(ctx, event, dir, XawstPositions, True, False);
#ifndef OLDXAW
    if (mul == 1)
	_XawSourceSetUndoErase((TextSrcObject)ctx->text.source,
			       dir == XawsdLeft ? -1 : 1);
#endif
}

/*ARGSUSED*/
static void
DeleteForwardChar(Widget w, XEvent *event, String *p, Cardinal *n)
{
    DeleteChar(w, event, XawsdRight);
}

/*ARGSUSED*/
static void
DeleteBackwardChar(Widget w, XEvent *event, String *p, Cardinal *n)
{
    DeleteChar(w, event, XawsdLeft);
}

static void
DeleteForwardWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    XawTextScanType type;

    if (*num_params && (*params[0] == 'A' || *params[0] == 'a'))
	type = XawstAlphaNumeric;
    else
	type = XawstWhiteSpace;

    DeleteOrKill((TextWidget)w, event, XawsdRight, type, False, False);
}

static void
DeleteBackwardWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    XawTextScanType type;

    if (*num_params && (*params[0] == 'A' || *params[0] == 'a'))
	type = XawstAlphaNumeric;
    else
	type = XawstWhiteSpace;

    DeleteOrKill((TextWidget)w, event, XawsdLeft, type, False, False);
}

static void
KillForwardWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    XawTextScanType type;

    if (*num_params && (*params[0] == 'A' || *params[0] == 'a'))
	type = XawstAlphaNumeric;
    else
	type = XawstWhiteSpace;

    DeleteOrKill((TextWidget)w, event, XawsdRight, type, False, True);
}

static void
KillBackwardWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    XawTextScanType type;

    if (*num_params && (*params[0] == 'A' || *params[0] == 'a'))
	type = XawstAlphaNumeric;
    else
	type = XawstWhiteSpace;

    DeleteOrKill((TextWidget) w, event, XawsdLeft, type, False, True);
}

/*ARGSUSED*/
static void
KillToEndOfLine(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition end_of_line;
    XawTextScanDirection dir = XawsdRight;
    short mult = MULT(ctx);

    if (mult < 0) {
	dir = XawsdLeft;
	mult = -mult;
    }

    StartAction(ctx, event);
    end_of_line = SrcScan(ctx->text.source, ctx->text.insertPos, XawstEOL,
			  dir, mult, False);
    if (end_of_line == ctx->text.insertPos)
	end_of_line = SrcScan(ctx->text.source, ctx->text.insertPos, XawstEOL,
			      dir, mult, True);

    if (dir == XawsdRight)
	_DeleteOrKill(ctx, ctx->text.insertPos, end_of_line, True);
    else
	_DeleteOrKill(ctx, end_of_line, ctx->text.insertPos, True);
    EndAction(ctx);
}

/*ARGSUSED*/
static void
KillToEndOfParagraph(Widget w, XEvent *event, String *p, Cardinal *n)
{
    DeleteOrKill((TextWidget)w, event, XawsdRight, XawstParagraph, False, True);
}

void
_XawTextZapSelection(TextWidget ctx, XEvent *event, Bool kill)
{
    StartAction(ctx, event);
    _DeleteOrKill(ctx, ctx->text.s.left, ctx->text.s.right, kill);
    EndAction(ctx);
}

/*ARGSUSED*/
static void
KillCurrentSelection(Widget w, XEvent *event, String *p, Cardinal *n)
{
    _XawTextZapSelection((TextWidget) w, event, True);
}

#ifndef OLDXAW
/*ARGSUSED*/
static void
KillRingYank(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition insertPos = ctx->text.insertPos;
    Bool first_yank = False;

    if (ctx->text.s.left != ctx->text.s.right)
	XawTextUnsetSelection((Widget)ctx);

    StartAction(ctx, event);

    if (ctx->text.kill_ring_ptr == NULL) {
	ctx->text.kill_ring_ptr = &kill_ring_prev;
	++ctx->text.kill_ring_ptr->refcount;
	ctx->text.s.left = ctx->text.s.right = insertPos;
	first_yank = True;
    }
    if (ctx->text.kill_ring_ptr) {
	int mul = MULT(ctx);
	XawTextBlock text;

	if (!first_yank) {
	    if (mul < 0)
		mul = 1;
	    --ctx->text.kill_ring_ptr->refcount;
	    while (mul--) {
		if ((ctx->text.kill_ring_ptr = ctx->text.kill_ring_ptr->next) == NULL)
		    ctx->text.kill_ring_ptr = &kill_ring_null;
	    }
	    ++ctx->text.kill_ring_ptr->refcount;
	}
	text.firstPos = 0;
	text.length = ctx->text.kill_ring_ptr->length;
	text.ptr = ctx->text.kill_ring_ptr->contents;
	text.format = ctx->text.kill_ring_ptr->format;

	if (_XawTextReplace(ctx, ctx->text.s.left, insertPos, &text) == XawEditDone) {
	    ctx->text.kill_ring = KILL_RING_YANK;
	    ctx->text.insertPos = ctx->text.s.left + text.length;
	}
    }
    else
	XBell(XtDisplay(w), 0);

    EndAction(ctx);
}
#endif /* OLDXAW */

/*ARGSUSED*/
static void
DeleteCurrentSelection(Widget w, XEvent *event, String *p, Cardinal *n)
{
    _XawTextZapSelection((TextWidget)w, event, False);
}

#ifndef OLDXAW
#define CHECK_SAVE()						\
	if (save && !save->ptr)					\
	    save->ptr = _XawTextGetText(ctx, save->firstPos,	\
		save->firstPos + save->length)
static Bool
StripSpaces(TextWidget ctx, XawTextPosition left, XawTextPosition right,
	    XawTextPosition *pos, int num_pos, XawTextBlock *save)
{
    Bool done, space;
    int i, cpos, count = 0;
    XawTextBlock block, text;
    XawTextPosition ipos, position = left, tmp = left;

    text.firstPos = 0;
    text.format = XawFmt8Bit;
    text.ptr = " ";
    text.length = 1;

    position = XawTextSourceRead(ctx->text.source, position,
				 &block, right - left);
    done = False;
    space = False;
    /* convert tabs and returns to spaces */
    while (!done) {
	if (XawTextFormat(ctx, XawFmt8Bit)) {
	    for (i = 0; i < block.length; i++)
		if (block.ptr[i] == '\t' || block.ptr[i] == '\n') {
		    space = True;
		    break;
		}
	}
	else {
	    wchar_t *wptr = (wchar_t*)block.ptr;
	    for (i = 0; i < block.length; i++)
		if (wptr[i] == _Xaw_atowc('\t') || wptr[i] == _Xaw_atowc('\n')) {
		    space = True;
		    break;
		}
	}
	if (space) {
	    CHECK_SAVE();
	    if (_XawTextReplace(ctx, tmp + i, tmp + i + 1, &text))
		return (False);
	    space = False;
	}
	tmp += i;
	position = XawTextSourceRead(ctx->text.source, tmp,
				     &block, right - tmp);
	if (block.length == 0 || tmp == position || tmp >= right)
	    done = True;
    }

    text.ptr = "";
    text.length = 0;
    position = tmp = left;
    position = XawTextSourceRead(ctx->text.source, position,
				 &block, right - left);
    ipos = ctx->text.insertPos;
    done = False;
    while (!done) {
	if (XawTextFormat(ctx, XawFmt8Bit)) {
	    for (i = 0; i < block.length; i++)
		if (block.ptr[i] == ' ')
		    ++count;
		else if (count == 1)
		    count = 0;
		else if (count)
		    break;
	}
	else {
	    wchar_t *wptr = (wchar_t*)block.ptr;
	    for (i = 0; i < block.length; i++)
		if (wptr[i] == _Xaw_atowc(' '))
		    ++count;
		else if (count == 1)
		    count = 0;
		else if (count)
		    break;
	}
	if (--count > 0) {
	    CHECK_SAVE();
	    if (_XawTextReplace(ctx, tmp + i - count, tmp + i, &text))
		return (False);
	    right -= count;
	    if (num_pos) {
		for (cpos = 0; cpos < num_pos; cpos++) {
		    if (tmp + i - count < pos[cpos]) {
			if (tmp + i < pos[cpos])
			    pos[cpos] -= count;
			else
			    pos[cpos] = tmp + i - count;
		    }
		}
	    }
	    else {
		if (tmp + i - count < ipos) {
		    if (tmp + i < ipos)
			ipos -= count;
		    else
			ipos = tmp + i - count;
		}
	    }
	    tmp += i - count;
	}
	else
	    tmp += i + 1;
	count = 0;
	position = XawTextSourceRead(ctx->text.source, tmp,
				     &block, right - tmp);
	if (block.length == 0 || tmp == position || tmp >= right)
	    done = True;
    }
    if (!num_pos)
	ctx->text.insertPos = ipos;

    return (True);
}

static Bool
Tabify(TextWidget ctx, XawTextPosition left, XawTextPosition right,
       XawTextPosition *pos, int num_pos, XawTextBlock *save)
{
    Bool done, zero;
    int i, cpos, count = 0, column = 0, offset = 0;
    XawTextBlock text, block;
    XawTextPosition ipos, position = left, tmp = left;
    TextSinkObject sink = (TextSinkObject)ctx->text.sink;
    short *char_tabs = sink->text_sink.char_tabs;
    int tab_count = sink->text_sink.tab_count;
    int tab_index = 0, tab_column = 0, TAB_SIZE = DEFAULT_TAB_SIZE;

    text.firstPos = 0;
    text.ptr = "\t";
    text.format = XawFmt8Bit;
    text.length = 1;

    position = XawTextSourceRead(ctx->text.source, position,
				 &block, right - left);
    ipos = ctx->text.insertPos;
    done = zero = False;
    if (tab_count)
	TAB_SIZE = *char_tabs;
    while (!done) {
	if (XawTextFormat(ctx, XawFmt8Bit)) {
	    for (i = 0; i < block.length; i++) {
		++offset;
		++column;
		if (tab_count) {
		    if (column > tab_column + char_tabs[tab_index]) {
			TAB_SIZE = tab_index < tab_count - 1 ? char_tabs[tab_index + 1] - char_tabs[tab_index] : *char_tabs;
			if (++tab_index >= tab_count) {
			    tab_column += char_tabs[tab_count - 1];
			    tab_index = 0;
			}
		    }
		}
		if (block.ptr[i] == ' ') {
		    if (++count > TAB_SIZE)
			count %= TAB_SIZE;
		    if ((tab_count && column == tab_column + char_tabs[tab_index]) ||
			(!tab_count && column % TAB_SIZE == 0)) {
			if (count % (TAB_SIZE + 1) > 1)
			    break;
			else
			    count = 0;
		    }
		}
		else {
		    if (block.ptr[i] == '\n') {
			zero = True;
			break;
		    }
		    count = 0;
		}
	    }
	}
	else {
	    wchar_t *wptr = (wchar_t*)block.ptr;
	    for (i = 0; i < block.length; i++) {
		++offset;
		++column;
		if (tab_count) {
		    if (column > tab_column + char_tabs[tab_index]) {
			TAB_SIZE = tab_index < tab_count - 1 ? char_tabs[tab_index + 1] - char_tabs[tab_index] : *char_tabs;
			if (++tab_index >= tab_count) {
			    tab_column += char_tabs[tab_count - 1];
			    tab_index = 0;
			}
		    }
		}
		if (wptr[i] == _Xaw_atowc(' ')) {
		    if (++count > TAB_SIZE)
			count %= TAB_SIZE;
		    if ((tab_count && column == tab_column + char_tabs[tab_index]) ||
			(!tab_count && column % TAB_SIZE == 0)) {
			if (count % (TAB_SIZE + 1) > 1)
			    break;
			else
			    count = 0;
		    }
		}
		else {
		    if (wptr[i] == _Xaw_atowc('\n')) {
			zero = True;
			break;
		    }
		    count = 0;
		}
	    }
	}
	count %= TAB_SIZE + 1;
	if (!zero && count > 1 && i < block.length) {
	    CHECK_SAVE();
	    if (_XawTextReplace(ctx, tmp + i - count + 1, tmp + i + 1, &text))
		return (False);
	    right -= count - 1;
	    offset -= count - 1;
	    if (num_pos) {
		for (cpos = 0; cpos < num_pos; cpos++) {
		    if (tmp + i - count + 1 < pos[cpos]) {
			if (tmp + i + 1 < pos[cpos])
			    pos[cpos] -= count;
			else
			    pos[cpos] = tmp + i - count + 1;
			++pos[cpos];
		    }
		}
	    }
	    else {
		if (tmp + i - count + 1 < ipos) {
		    if (tmp + i + 1 < ipos)
			ipos -= count;
		    else
			ipos = tmp + i - count + 1;
		    ++ipos;
		}
	    }
	}
	if (count)
	    --count;
	if (zero) {
	    count = column = 0;
	    zero = False;
	    if (tab_count) {
		tab_column = tab_index = 0;
		TAB_SIZE = *char_tabs;
	    }
	}
	else if (i < block.length)
	    count = 0;
	tmp = left + offset;
	position = XawTextSourceRead(ctx->text.source, tmp,
				     &block, right - tmp);
	if (tmp == position || tmp >= right)
	    done = True;
    }
    if (!num_pos)
	ctx->text.insertPos = ipos;

    return (True);
}

static Bool
Untabify(TextWidget ctx, XawTextPosition left, XawTextPosition right,
	 XawTextPosition *pos, int num_pos, XawTextBlock *save)
{
    Bool done, zero;
    int i, cpos, count = 0, diff = 0;
    XawTextBlock block, text;
    XawTextPosition ipos, position = left, tmp = left;
    TextSinkObject sink = (TextSinkObject)ctx->text.sink;
    short *char_tabs = sink->text_sink.char_tabs;
    int tab_count = sink->text_sink.tab_count;
    int tab_index = 0, tab_column = 0, tab_base = 0;
    static char *tabs = "        ";

    text.firstPos = 0;
    text.format = XawFmt8Bit;
    text.ptr = tabs;

    position = XawTextSourceRead(ctx->text.source, position,
				 &block, right - left);
    ipos = ctx->text.insertPos;
    done = False;
    zero = False;
    while (!done) {
	if (XawTextFormat(ctx, XawFmt8Bit))
	    for (i = 0; i < block.length; i++) {
		if (block.ptr[i] != '\t') {
		    ++count;
		    if (block.ptr[i] == '\n') {
			zero = True;
			break;
		    }
		}
		else
		    break;
	}
	else {
	    wchar_t *wptr = (wchar_t*)block.ptr;
	    for (i = 0; i < block.length; i++)
		if (wptr[i] != _Xaw_atowc('\t')) {
		    ++count;
		    if (wptr[i] != _Xaw_atowc('\n')) {
			zero = True;
			break;
		    }
		}
		else
		    break;
	}
	if (!zero && i < block.length) {
	    if (tab_count) {
		while (tab_base + tab_column <= count) {
		    for (; tab_index < tab_count; ++tab_index)
			if (tab_base + char_tabs[tab_index] > count) {
			    tab_column = char_tabs[tab_index];
			    break;
			}
		    if (tab_index >= tab_count) {
			tab_base += char_tabs[tab_count - 1];
			tab_column = tab_index = 0;
		    }
		}
		text.length = (tab_base + tab_column) - count;
		if (text.length > 8) {
		    int j;

		    text.ptr = XtMalloc(text.length);
		    for (j = 0; j < text.length; j++)
			text.ptr[j] = ' ';
		}
		else
		    text.ptr = tabs;
	    }
	    else
		text.length = DEFAULT_TAB_SIZE - (count % DEFAULT_TAB_SIZE);
	    CHECK_SAVE();
	    if (_XawTextReplace(ctx, tmp + i, tmp + i + 1, &text)) {
		if (tab_count && text.length > 8)
		    XtFree(text.ptr);
		return (False);
	    }
	    if (tab_count && text.length > 8)
		XtFree(text.ptr);
	    count += text.length;
	    right += text.length - 1;
	    if (num_pos) {
		for (cpos = 0; cpos < num_pos; cpos++) {
		    if (tmp + i < pos[cpos]) {
			if (tmp + i + 1 < pos[cpos])
			    --pos[cpos];
			else
			    pos[cpos] = tmp + i;
			pos[cpos] += text.length;
		    }
		}
	    }
	    else {
		if (tmp + i < ipos) {
		    if (tmp + i + 1 < ipos)
			--ipos;
		    else
			ipos = tmp + i;
		    ipos += text.length;
		}
	    }
	}
	tmp = left + count + diff;
	if (zero) {
	    diff += count;
	    count = 0;
	    zero = False;
	    if (tab_count)
		tab_base = tab_column = tab_index = 0;
	}
	position = XawTextSourceRead(ctx->text.source, tmp,
				     &block, right - tmp);
	if (tmp == position || tmp >= right)
	    done = True;
    }
    if (!num_pos)
	ctx->text.insertPos = ipos;

    return (True);
}

static int
FormatText(TextWidget ctx, XawTextPosition left, Bool force,
	   XawTextPosition *pos, int num_pos)
{
    char *ptr = NULL;
    Bool freepos = False, undo, paragraph = pos != NULL;
    int i, result;
    XawTextBlock block, *text;
    XawTextPosition end = ctx->text.lastPos, buf[32];
    TextSrcObject src = (TextSrcObject)ctx->text.source;
    XawTextPosition right = SrcScan(ctx->text.source, left, XawstEOL,
				    XawsdRight, 1, False);

    undo = src->textSrc.enable_undo && src->textSrc.undo_state == False;
    if (undo) {
	if (!pos) {
	    num_pos = src->textSrc.num_text;
	    pos = XawStackAlloc(sizeof(XawTextPosition) * num_pos, buf);
	    for (i = 0; i < num_pos; i++)
		pos[i] = ((TextWidget)src->textSrc.text[i])->text.insertPos;
	    freepos = True;
	}
	else
	    freepos = False;
	src->textSrc.undo_state = True;
	block.ptr = NULL;
	block.firstPos = left;
	block.length = right - left;
	text = &block;
    }
    else
	text = NULL;

    result = DoFormatText(ctx, left, force, 1, text, pos, num_pos, paragraph);
    if (undo && result == XawEditDone && block.ptr) {
	char *lbuf, *rbuf;
	unsigned llen, rlen, size;

	ptr = lbuf = block.ptr;
	llen = block.length;
	rlen = llen + (ctx->text.lastPos - end);

	block.firstPos = 0;
	block.format = _XawTextFormat(ctx);

	rbuf = _XawTextGetText(ctx, left, left + rlen);

	size = XawTextFormat(ctx, XawFmtWide) ? sizeof(wchar_t) : sizeof(char);
	if (llen != rlen || memcmp(lbuf, rbuf, llen * size)) {
	    block.ptr = lbuf;
	    block.length = llen;
	    _XawTextReplace(ctx, left, left + rlen, &block);

	    src->textSrc.undo_state = False;
	    block.ptr = rbuf;
	    block.length = rlen;
	    _XawTextReplace(ctx, left, left + llen, &block);
	}
	else
	    src->textSrc.undo_state = False;
	XtFree(rbuf);
    }
    if (undo) {
	src->textSrc.undo_state = False;
	if (freepos) {
	    for (i = 0; i < num_pos; i++) {
		TextWidget tw = (TextWidget)src->textSrc.text[i];
		tw->text.insertPos = XawMin(XawMax(0, pos[i]), tw->text.lastPos);
	    }
	    XawStackFree(pos, buf);
	}
	if (ptr)
	    XtFree(ptr);
    }

    return (result);
}

static int
DoFormatText(TextWidget ctx, XawTextPosition left, Bool force, int level,
	     XawTextBlock *save, XawTextPosition *pos, int num_pos,
	     Bool paragraph)
{
    XawTextPosition right = SrcScan(ctx->text.source, left, XawstEOL,
				    XawsdRight, 1, False);
    XawTextPosition position, tmp, ipos;
    XawTextBlock block, text;
    char buf[128];
    wchar_t *wptr;
    int i, count, cpos;
    Bool done, force2 = force, recurse = False;

    position = XawTextSourceRead(ctx->text.source, left, &block, right - left);
    if (block.length == 0 || left >= right ||
	(level == 1 && ((XawTextFormat(ctx, XawFmt8Bit) &&
	 block.ptr[0] != ' ' &&
	 block.ptr[0] != '\t' &&
	 !isalnum(*(unsigned char*)block.ptr)) ||
	(XawTextFormat(ctx, XawFmtWide) &&
	 _Xaw_atowc(XawSP) != *(wchar_t*)block.ptr &&
	 _Xaw_atowc(XawTAB) != *(wchar_t*)block.ptr &&
	 !iswalnum(*(wchar_t*)block.ptr)))))
	return (XawEditDone);

    if (level == 1 && !paragraph) {
	tmp = ctx->text.lastPos;
	if (Untabify(ctx, left, right, pos, num_pos, save) == False)
	    return (XawEditError);
	right += ctx->text.lastPos - tmp;
	position = XawTextSourceRead(ctx->text.source, left, &block,
				     right - left);
    }

    text.firstPos = 0;
    text.format = XawFmt8Bit;

    ipos = ctx->text.insertPos;
    count = 0;
    done = False;
    while (!done) {
	if (XawTextFormat(ctx, XawFmt8Bit)) {
	    for (i = 0; i < block.length; i++)
		if (block.ptr[i] == ' ')
		    ++count;
		else {
		    done = True;
		    break;
		}
	}
	else {
	    wptr = (wchar_t*)block.ptr;
	    for (i = 0; i < block.length; i++)
		if (wptr[i] == _Xaw_atowc(' '))
		    ++count;
		else {
		    done = True;
		    break;
		}
	}
	tmp = position;
	position = XawTextSourceRead(ctx->text.source, position,
				     &block, right - position);
	if (tmp == position)
	    done = True;
    }
    position = left + count;
    if (count < ctx->text.left_column) {
	int bytes = ctx->text.left_column - count;

	text.ptr = XawStackAlloc(bytes, buf);
	text.length = bytes;
	for (i = 0; i < bytes; i++)
	    text.ptr[i] = ' ';
	CHECK_SAVE();
	if (_XawTextReplace(ctx, left, left, &text)) {
	    XawStackFree(text.ptr, buf);
	    return (XawEditError);
	}
	XawStackFree(text.ptr, buf);
	right += bytes;
	if (num_pos) {
	    for (cpos = 0; cpos < num_pos; cpos++)
		if (pos[cpos] >= left)
		    pos[cpos] += bytes;
	}
	if (ipos >= left)
	    ipos += bytes;
	count += bytes;
    }

    done = False;
    if (!paragraph && level == 1
	&& ipos <= right && ipos - left > ctx->text.right_column) {
	XawTextPosition len = ctx->text.lastPos;
	int skip = ctx->text.justify == XawjustifyRight
		|| ctx->text.justify == XawjustifyCenter ?
		ctx->text.left_column : count;

	if (pos)
	    for (i = 0; i < num_pos; i++)
		if (pos[i] == ipos)
		    break;

	StripSpaces(ctx, left + skip, right, pos, num_pos, save);
	right += ctx->text.lastPos - len;
	if (pos && i < num_pos)
	    ipos = pos[i];
	else
	    ipos = ctx->text.insertPos;
	done = ipos - left > ctx->text.right_column;
	count = skip + (count == skip + 1);
    }
    if ((paragraph || done) && right - left > ctx->text.right_column) {
	position = tmp = right;
	XawTextSourceRead(ctx->text.source, position - 1, &block, 1);
	if (block.length &&
	    ((XawTextFormat(ctx, XawFmt8Bit) &&
	     block.ptr[0] == ' ') ||
	    (XawTextFormat(ctx, XawFmtWide) &&
	     _Xaw_atowc(XawSP) == *(wchar_t*)block.ptr)))
	    --position;
	while (position - left > ctx->text.right_column) {
	    tmp = position;
	    position = SrcScan(ctx->text.source, position,
			       XawstWhiteSpace, XawsdLeft, 1, True);
	}
	if (position <= left + ctx->text.left_column)
	    position = tmp;
	if (position > left && position - left > ctx->text.left_column
	    && position != right) {
	    text.ptr = "\n";
	    text.length = 1;
	    CHECK_SAVE();
	    if (_XawTextReplace(ctx, position, position + 1, &text))
		return (XawEditError);
	    right = position;
	    recurse = True;
	    force = True;
	}
    }

    if (force) {
	if (ctx->text.justify == XawjustifyCenter)
	    count = ctx->text.right_column - (count - ctx->text.left_column);
	else
	    count = ctx->text.right_column;
	if (count > right - left)
	    count -= right - left;
	else
	    count = 0;
    }
    else
	count = 0;
    if (count > 0) {
	switch (ctx->text.justify) {
	    case XawjustifyLeft:
		break;
	    case XawjustifyRight:
	    case XawjustifyCenter:
		if (ctx->text.justify == XawjustifyCenter) {
		    int alnum = 0;

		    if (!(count & 1)) {
			XawTextSourceRead(ctx->text.source, right, &block, 1);
			if ((XawTextFormat(ctx, XawFmt8Bit)
			     && isalnum(*(unsigned char*)block.ptr)) ||
			    (XawTextFormat(ctx, XawFmtWide)
			     && iswalnum(*(wchar_t*)block.ptr)))
			    alnum = 1;
		    }
		    count = (count + alnum) >> 1;
		}
		text.ptr = XawStackAlloc(count, buf);
		text.length = count;
		for (i = 0; i < count; i++)
		    text.ptr[i] = ' ';
		CHECK_SAVE();
		if (_XawTextReplace(ctx, left, left, &text)) {
		    XawStackFree(text.ptr, buf);
		    return (XawEditError);
		}
		XawStackFree(text.ptr, buf);
		position += count;
		right += count;
		if (num_pos) {
		    for (cpos = 0; cpos < num_pos; cpos++)
			if (pos[cpos] > left)
			    pos[cpos] += count;
		}
		else if (ipos > left)
		    ipos += count;
		break;
	    case XawjustifyFull:
		i = 0;
		tmp = left;
		/*CONSTCOND*/
		while (True) {
		    tmp = SrcScan(ctx->text.source, tmp, XawstWhiteSpace,
				  XawsdRight, 1, True);
		    if (tmp < right)
			++i;
		    else
			break;
		}
		if (i) {
		    double inc, ii;
		    int bytes, steps;

		    bytes = count;
		    inc = ii = (count + .5) / (double)i;

		    steps = count;
		    text.ptr = XawStackAlloc(steps, buf);
		    for (i = 0; i < steps; i++)
			text.ptr[i] = ' ';
		    tmp = left;
		    CHECK_SAVE();
		    while (bytes) {
			steps = 1;
			while (inc + ii < 1) {
			    ++steps;
			    inc += ii;
			}
			tmp = SrcScan(ctx->text.source, tmp, XawstWhiteSpace,
				      XawsdRight, steps, True);
			if (bytes > inc)
			    text.length = (int)inc;
			else
			    text.length = bytes;
			bytes -= text.length;
			if (_XawTextReplace(ctx, tmp, tmp, &text)) {
			    XawStackFree(text.ptr, buf);
			    return (XawEditError);
			}
			if (num_pos) {
			    for (cpos = 0; cpos < num_pos; cpos++)
				if (tmp <= pos[cpos])
				    pos[cpos] += text.length;
			}
			else if (tmp <= ipos)
			    ipos += text.length;
			inc -= (int)inc;
			inc += ii;
		    }
		    position += count;
		    right += count;
		    XawStackFree(text.ptr, buf);
		}
		break;
	}
    }

    if (!num_pos)
	ctx->text.insertPos = XawMin(ipos, ctx->text.lastPos);

    return (recurse ? DoFormatText(ctx, position + 1,
				   ctx->text.justify != XawjustifyFull
				   && (force2 || paragraph),
				   ++level, save, pos, num_pos, paragraph)
		 : XawEditDone);
}
#undef CHECK_SAVE

/*ARGSUSED*/
static void
Indent(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    TextSrcObject src = (TextSrcObject)ctx->text.source;
    XawTextPosition from, to, tmp, end = 0, *pos, *posbuf[32];
    char buf[32];
    XawTextBlock text;
    int i, spaces = MULT(ctx);
    char *lbuf = NULL, *rbuf;
    unsigned llen = 0, rlen, size;
    Bool undo = src->textSrc.enable_undo && src->textSrc.undo_state == False;
    Bool format = ctx->text.auto_fill
	&& ctx->text.left_column < ctx->text.right_column;

    text.firstPos = 0;
    text.format = XawFmt8Bit;
    text.ptr = "";

    StartAction(ctx, event);

    pos = XawStackAlloc(sizeof(XawTextPosition) * src->textSrc.num_text, posbuf);
    for (i = 0; i < src->textSrc.num_text; i++)
	pos[i] = ((TextWidget)src->textSrc.text[i])->text.insertPos;

    if (!GetBlockBoundaries(ctx, &from, &to)) {
	EndAction(ctx);
	XawStackFree(pos, posbuf);
	return;
    }

    if (undo) {
	llen = to - from;
	end = ctx->text.lastPos;
	lbuf = _XawTextGetText(ctx, from, to);
	src->textSrc.undo_state = True;
    }

    tmp = ctx->text.lastPos;
    if (!Untabify(ctx, from, to, pos, src->textSrc.num_text, NULL)) {
	XBell(XtDisplay(ctx), 0);
	EndAction(ctx);
	XawStackFree(pos, posbuf);
	if (undo) {
	    src->textSrc.undo_state = True;
	    XtFree(lbuf);
	}
	return;
    }
    to += ctx->text.lastPos - tmp;

    tmp = from;

    if (spaces > 0) {
	text.ptr = XawStackAlloc(spaces, buf);
	for (i = 0; i < spaces; i++)
	    text.ptr[i] = ' ';

	text.length = spaces;
	while (tmp < to) {
	    _XawTextReplace(ctx, tmp, tmp, &text);

	    for (i = 0; i < src->textSrc.num_text; i++)
		if (tmp < pos[i])
		    pos[i] += spaces;

	    to += spaces;
	    tmp = SrcScan(ctx->text.source, tmp, XawstEOL, XawsdRight, 1, True);
	}
	XawStackFree(text.ptr, buf);
    }
    else {
	int min = 32767;

	text.length = 0;
	tmp = from;

	/* find the amount of spaces to cut */
	while (tmp < to) {
	    (void)BlankLine(w, tmp, &i);
	    if (i < min)
		min = i;
	    tmp = SrcScan(ctx->text.source, tmp, XawstEOL, XawsdRight, 1, True);
	}
	spaces = XawMin(-spaces, min);

	/* cut the spaces */
	tmp = from;
	while (tmp < to) {
	    _XawTextReplace(ctx, tmp, tmp + spaces, &text);

	    for (i = 0; i < src->textSrc.num_text; i++)
		if (tmp < pos[i]) {
		    if (tmp + spaces < pos[i])
			pos[i] -= spaces;
		    else
			pos[i] = tmp;
		}

	    to -= spaces;
	    tmp = SrcScan(ctx->text.source, tmp, XawstEOL, XawsdRight, 1, True);
	}
    }

    if (!format)
	Tabify(ctx, from, to, pos, src->textSrc.num_text, NULL);

    if (undo) {
	rlen = llen + (ctx->text.lastPos - end);
	rbuf = _XawTextGetText(ctx, from, from + rlen);

	text.format = _XawTextFormat(ctx);
	size = XawTextFormat(ctx, XawFmtWide) ? sizeof(wchar_t) : sizeof(char);
	if (llen != rlen || memcmp(lbuf, rbuf, llen * size)) {
	    text.ptr = lbuf;
	    text.length = llen;
	    _XawTextReplace(ctx, from, from + rlen, &text);

	    src->textSrc.undo_state = False;
	    text.ptr = rbuf;
	    text.length = rlen;
	    _XawTextReplace(ctx, from, from + llen, &text);
	}
	else
	    src->textSrc.undo_state = False;
	XtFree(lbuf);
	XtFree(rbuf);
    }

    for (i = 0; i < src->textSrc.num_text; i++) {
	TextWidget tw = (TextWidget)src->textSrc.text[i];

	tw->text.insertPos = XawMin(XawMax(0, pos[i]), tw->text.lastPos);
    }
    XawStackFree(pos, posbuf);
    ctx->text.showposition = True;

    EndAction(ctx);
}

/*ARGSUSED*/
static void
ToggleOverwrite(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

    ctx->text.overwrite = !ctx->text.overwrite;

    /* call information callback */
    _XawTextSetLineAndColumnNumber(ctx, True);
}
#endif /* OLDXAW */

/*
 * Insertion Routines
 */
static int
InsertNewLineAndBackupInternal(TextWidget ctx)
{
    int count, error = XawEditDone, mult = MULT(ctx);
#ifndef OLDXAW
    XawTextPosition position;
#endif
    XawTextBlock text;
    char buf[32];

    if (mult < 0) {
	ctx->text.mult = 1;
	return (XawEditError);
    }

    text.format = _XawTextFormat(ctx);
    text.length = mult;
    text.firstPos = 0;

    if (text.format == XawFmtWide) {
	wchar_t *wptr;

	text.ptr =  XawStackAlloc(sizeof(wchar_t) * mult, buf);
	wptr = (wchar_t *)text.ptr;
	for (count = 0; count < mult; count++)
	    wptr[count] = _Xaw_atowc(XawLF);
    }
    else {
	text.ptr = XawStackAlloc(sizeof(char) * mult, buf);
	for (count = 0; count < mult; count++)
	    text.ptr[count] = XawLF;
    }

#ifndef OLDXAW
    position = SrcScan(ctx->text.source, ctx->text.insertPos,
		       XawstEOL, XawsdLeft, 1, False);
#endif
    if (_XawTextReplace(ctx, ctx->text.insertPos, ctx->text.insertPos, &text)) {
	XBell( XtDisplay(ctx), 50);
	error = XawEditError;
    }
    else {
	ctx->text.showposition = TRUE;
	ctx->text.insertPos += text.length;
    }

    XawStackFree(text.ptr, buf);

#ifndef OLDXAW
    if (ctx->text.auto_fill && error == XawEditDone)
	(void)FormatText(ctx, position, ctx->text.justify != XawjustifyFull,
			 NULL, 0);
#endif

    return (error);
}

/*ARGSUSED*/
static void
InsertNewLineAndBackup(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition insertPos = ctx->text.insertPos;

    StartAction((TextWidget)w, event);
    (void)InsertNewLineAndBackupInternal(ctx);
    ctx->text.insertPos = SrcScan(ctx->text.source, insertPos, XawstEOL,
				  XawsdRight, 1, False);
    EndAction((TextWidget)w);
}

static int
LocalInsertNewLine(TextWidget ctx, XEvent *event)
{
    int error;

    StartAction(ctx, event);
    error = InsertNewLineAndBackupInternal(ctx);
    ctx->text.from_left = -1;
    EndAction(ctx);

    return (error);
}

/*ARGSUSED*/
static void
InsertNewLine(Widget w, XEvent *event, String *p, Cardinal *n)
{
    (void)LocalInsertNewLine((TextWidget)w, event);
}

/*ARGSUSED*/
static void
InsertNewLineAndIndent(Widget w, XEvent *event, String *p, Cardinal *n)
{
    XawTextBlock text;
    XawTextPosition pos1;
    int length;
    TextWidget ctx = (TextWidget)w;
    String line_to_ip;

    StartAction(ctx, event);
    pos1 = SrcScan(ctx->text.source, ctx->text.insertPos,
		   XawstEOL, XawsdLeft, 1, False);

    line_to_ip = _XawTextGetText(ctx, pos1, ctx->text.insertPos);

    text.format = _XawTextFormat(ctx);
    text.firstPos = 0;

    if (text.format == XawFmtWide) {
	wchar_t *ptr;

	text.ptr = XtMalloc((2 + wcslen((wchar_t*)line_to_ip))
			    * sizeof(wchar_t));
	ptr = (wchar_t*)text.ptr;
	ptr[0] = _Xaw_atowc(XawLF);
	wcscpy((wchar_t*)++ptr, (wchar_t*)line_to_ip);

	length = wcslen((wchar_t*)text.ptr);
	while (length && (iswspace(*ptr) || *ptr == _Xaw_atowc(XawTAB)))
	  ptr++, length--;
	*ptr = (wchar_t)0;
	text.length = wcslen((wchar_t*)text.ptr);
    }
    else {
	char *ptr;

	length = strlen(line_to_ip);
	text.ptr = XtMalloc((2 + length) * sizeof(char));
	ptr = text.ptr;
	ptr[0] = XawLF;
	strcpy(++ptr, line_to_ip);

	length++;
	while (length && (isspace(*ptr) || (*ptr == XawTAB)))
	    ptr++, length--;
	*ptr = '\0';
	text.length = strlen(text.ptr);
    }
    XtFree(line_to_ip);

    if (_XawTextReplace(ctx,ctx->text.insertPos, ctx->text.insertPos, &text)) {
	XBell(XtDisplay(ctx), 50);
	XtFree(text.ptr);
	EndAction(ctx);
	return;
    }

    XtFree(text.ptr);
    ctx->text.from_left = -1;
    ctx->text.insertPos = SrcScan(ctx->text.source, ctx->text.old_insert,
				  XawstPositions, XawsdRight, text.length, True);
    EndAction(ctx);
}

/*
 * Selection Routines
 */
static void
SelectWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition l, r;

    StartAction(ctx, event);
    l = SrcScan(ctx->text.source, ctx->text.insertPos,
		XawstWhiteSpace, XawsdLeft, 1, False);
    r = SrcScan(ctx->text.source, l, XawstWhiteSpace, XawsdRight, 1, False);
    _XawTextSetSelection(ctx, l, r, params, *num_params);
    EndAction(ctx);
}

static void
SelectAll(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

    StartAction(ctx, event);
    _XawTextSetSelection(ctx,zeroPosition,ctx->text.lastPos,params,*num_params);
    EndAction(ctx);
}

static void
ModifySelection(TextWidget ctx, XEvent *event,
		XawTextSelectionMode mode,
		XawTextSelectionAction action,
		String *params, Cardinal *num_params)
{
#ifndef OLDXAW
    int old_y = ctx->text.ev_y;
#endif

    StartAction(ctx, event);
    NotePosition(ctx, event);

#ifndef OLDXAW
    if (event->type == MotionNotify) {
	if (ctx->text.ev_y <= ctx->text.margin.top) {
	    if (old_y >= ctx->text.ev_y)
		XawTextScroll(ctx, -1, 0);
	}
	else if (ctx->text.ev_y >= XtHeight(ctx) - ctx->text.margin.bottom) {
	    if (old_y <= ctx->text.ev_y
		&& !IsPositionVisible(ctx, ctx->text.lastPos))
	      XawTextScroll(ctx, 1, 0);
	}
    }
#endif
    ctx->text.from_left = -1;
    _XawTextAlterSelection(ctx, mode, action, params, num_params);

    EndAction(ctx);
}

static void
SelectStart(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

#ifndef OLDXAW
    if (!ctx->text.selection_state) {
	ctx->text.selection_state = True;
#endif
	ModifySelection(ctx, event,
			XawsmTextSelect, XawactionStart, params, num_params);
#ifndef OLDXAW
    }
#endif
}

static void
SelectAdjust(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

#ifndef OLDXAW
    if (ctx->text.selection_state)
#endif
	ModifySelection(ctx, event,
			XawsmTextSelect, XawactionAdjust, params, num_params);
}

static void
SelectEnd(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

#ifndef OLDXAW
    if (ctx->text.selection_state) {
	ctx->text.selection_state = False;
#endif
	ModifySelection(ctx, event,
			XawsmTextSelect, XawactionEnd, params, num_params);
#ifndef OLDXAW
    }
#endif
}

static void
ExtendStart(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

#ifndef OLDXAW
    if (!ctx->text.selection_state) {
	ctx->text.selection_state = True;
#endif
	ModifySelection(ctx, event,
			XawsmTextExtend, XawactionStart, params, num_params);
#ifndef OLDXAW
    }
#endif
}

static void
ExtendAdjust(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

#ifndef OLDXAW
    if (ctx->text.selection_state)
#endif
	ModifySelection(ctx, event,
			XawsmTextExtend, XawactionAdjust, params, num_params);
}

static void
ExtendEnd(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

#ifndef OLDXAW
    if (ctx->text.selection_state) {
	ctx->text.selection_state = False;
#endif
	ModifySelection(ctx, event,
			XawsmTextExtend, XawactionEnd, params, num_params);
#ifndef OLDXAW
    }
#endif
}

static void
SelectSave(Widget  w, XEvent *event, String *params, Cardinal *num_params)
{
    int num_atoms;
    Atom *sel;
    Display *dpy = XtDisplay(w);
    Atom selections[256];

    StartAction((TextWidget)w, event);
    num_atoms = *num_params;
    if (num_atoms > 256)
	num_atoms = 256;
    for (sel=selections; --num_atoms >= 0; sel++, params++)
	*sel = XInternAtom(dpy, *params, False);
    num_atoms = *num_params;
    _XawTextSaltAwaySelection((TextWidget)w, selections, num_atoms);
    EndAction((TextWidget)w);
}

/*
 * Misc. Routines
 */
/*ARGSUSED*/
static void
SetKeyboardFocus(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    Widget shell, parent;

    shell = parent = w;
    while (parent) {
	if (XtIsShell(shell = parent))
	    break;
	parent = XtParent(parent);
    }
    XtSetKeyboardFocus(shell, w);
}

/*ARGSUSED*/
static void
RedrawDisplay(Widget w, XEvent *event, String *p, Cardinal *n)
{
    StartAction((TextWidget)w, event);
    _XawTextClearAndCenterDisplay((TextWidget)w);
    EndAction((TextWidget)w);
}

/* This is kind of a hack, but, only one text widget can have focus at
 * a time on one display. There is a problem in the implementation of the
 * text widget, the scrollbars can not be adressed via editres, since they
 * are not children of a subclass of composite.
 * The focus variable is required to make sure only one text window will
 * show a block cursor at one time.
 */
struct _focus { Display *display; Widget widget; };
static struct _focus *focus;
static Cardinal num_focus;

/*ARGSUSED*/
static void
DestroyFocusCallback(Widget w, XtPointer user_data, XtPointer call_data)
{
    struct _focus *f = (struct _focus*)(user_data);

    if (f->widget == w)
	f->widget = NULL;
}

/*ARGSUSED*/
static void
TextFocusIn(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    Bool display_caret = ctx->text.display_caret;
    int i;

    if (event->xfocus.detail == NotifyPointer)
	return;

    if (event->xfocus.send_event) {
	Window root, child;
	int rootx, rooty, x, y;
	unsigned int mask;

	if (ctx->text.hasfocus)
	    return;

	if (XQueryPointer(XtDisplay(w), XtWindow(w), &root, &child,
			  &rootx, &rooty, &x, &y, &mask)) {
	    if (child)
		return;
	}
    }

    /* Let the input method know focus has arrived. */
    _XawImSetFocusValues(w, NULL, 0);

    if (display_caret)
	StartAction(ctx, event);
    ctx->text.hasfocus = TRUE;
    if (display_caret)
	EndAction(ctx);

    for (i = 0; i < num_focus; i++)
	if (focus[i].display == XtDisplay(w))
	    break;
    if (i >= num_focus) {
	focus = (struct _focus*)
	    XtRealloc((XtPointer)focus, sizeof(struct _focus) * (num_focus + 1));
	i = num_focus;
	focus[i].widget = NULL;
	focus[i].display = XtDisplay(w);
	num_focus++;
    }
    if (focus[i].widget != w) {
	Widget old = focus[i].widget;

	focus[i].widget = w;
	if (old != NULL) {
	    TextFocusOut(old, event, p, n);
	    /* TextFocusOut may set it to NULL */
	    focus[i].widget = w;
	}
	XtAddCallback(w, XtNdestroyCallback,
		      DestroyFocusCallback, (XtPointer)&focus[i]);
    }
}

/*ARGSUSED*/
static void
TextFocusOut(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    Bool display_caret = ctx->text.display_caret;
    Widget shell;
    Window window;
    int i, revert;

    shell = w;
    while (shell) {
	if (XtIsShell(shell))
	   break;
	shell = XtParent(shell);
    }

    for (i = 0; i < num_focus; i++)
	if (focus[i].display == XtDisplay(w))
	    break;
    XGetInputFocus(XtDisplay(w), &window, &revert);
    if ((XtWindow(shell) == window &&
	 (i < num_focus && focus[i].widget == w))
	 || event->xfocus.detail == NotifyPointer)
	return;

    if (i < num_focus && focus[i].widget) {
	XtRemoveCallback(focus[i].widget, XtNdestroyCallback,
			 DestroyFocusCallback, (XtPointer)&focus[i]);
	focus[i].widget = NULL;
    }

    /* Let the input method know focus has left.*/
    _XawImUnsetFocus(w);

    if (display_caret)
	StartAction(ctx, event);
    ctx->text.hasfocus = FALSE;
    if (display_caret)
	EndAction(ctx);
}

/*ARGSUSED*/
static void
TextEnterWindow(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

    if ((event->xcrossing.detail != NotifyInferior) && event->xcrossing.focus
	&& !ctx->text.hasfocus)
	_XawImSetFocusValues(w, NULL, 0);
}

/*ARGSUSED*/
static void
TextLeaveWindow(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

    if ((event->xcrossing.detail != NotifyInferior) && event->xcrossing.focus
	&& !ctx->text.hasfocus)
	_XawImUnsetFocus(w);
}

/*
 * Function:
 *	AutoFill
 *	Arguments: ctx - The text widget.
 *
 * Description:
 *	  Breaks the line at the previous word boundry when
 *	called inside InsertChar.
 */
static void
AutoFill(TextWidget ctx)
{
    int width, height, x, line_num, max_width;
    XawTextPosition ret_pos;
    XawTextBlock text;
    XRectangle cursor;
    wchar_t wc_buf[2];

    for (line_num = 0; line_num < ctx->text.lt.lines ; line_num++)
	if (ctx->text.lt.info[line_num].position >= ctx->text.insertPos)
	    break;
    if (line_num)
	line_num--;		/* backup a line. */

    XawTextSinkGetCursorBounds(ctx->text.sink, &cursor);
    max_width = Max(0, (int)XtWidth(ctx) - RHMargins(ctx) - cursor.width);

    x = ctx->text.r_margin.left;
    XawTextSinkFindPosition(ctx->text.sink, ctx->text.lt.info[line_num].position,
			    x, max_width, True, &ret_pos,
			    &width, &height);

    if (ret_pos <= ctx->text.lt.info[line_num].position
	|| ret_pos >= ctx->text.insertPos || ret_pos < 1)
	return;

    XawTextSourceRead(ctx->text.source, ret_pos - 1, &text, 1);

    if (XawTextFormat(ctx, XawFmtWide)) {
	wc_buf[0] = *(wchar_t *)text.ptr;
	if (wc_buf[0] != _Xaw_atowc(XawSP) && wc_buf[0] != _Xaw_atowc(XawTAB))
	    /* Only eats white spaces */
	    return;

	text.format = XawFmtWide;
	text.ptr = (char *)wc_buf;
	wc_buf[0] = _Xaw_atowc(XawLF);
	wc_buf[1] = 0;
    }
    else {
	if (text.ptr[0] != XawSP && text.ptr[0] != XawTAB)
	    /* Only eats white spaces */
	    return;

	text.format = XawFmt8Bit;
	text.ptr = "\n";
    }
    text.length = 1;
    text.firstPos = 0;

    if (_XawTextReplace(ctx, ret_pos - 1, ret_pos, &text))
	XBell(XtDisplay((Widget)ctx), 0);

    if (++ctx->text.insertPos > ctx->text.lastPos)
	ctx->text.insertPos = ctx->text.lastPos;
}

/*ARGSUSED*/
static void
InsertChar(Widget w, XEvent *event, String *p, Cardinal *n)
{
    TextWidget ctx = (TextWidget)w;
    char *ptr, strbuf[128], ptrbuf[512];
    int count, error, mult = MULT(ctx);
    KeySym keysym;
    XawTextBlock text;
#ifndef OLDXAW
    Bool format = False;
#endif
    XawTextPosition from, to;

    if (XtIsSubclass (ctx->text.source, (WidgetClass) multiSrcObjectClass))
	text.length = _XawImWcLookupString(w, &event->xkey, (wchar_t*)strbuf,
					   sizeof(strbuf), &keysym);
    else
	text.length = _XawLookupString(w, (XKeyEvent*)event, strbuf,
				       sizeof(strbuf), &keysym);

    if (text.length == 0)
	return;

    if (mult < 0) {
	ctx->text.mult = 1;
	return;
    }

    text.format = _XawTextFormat(ctx);
    if (text.format == XawFmtWide) {
	text.ptr = ptr = XawStackAlloc(sizeof(wchar_t) * text.length
				       * mult, ptrbuf);
	for (count = 0; count < mult; count++) {
	    memcpy((char*)ptr, (char *)strbuf, sizeof(wchar_t) * text.length);
	    ptr += sizeof(wchar_t) * text.length;
	}
#ifndef OLDXAW
	if (mult == 1)
	    format = ctx->text.left_column < ctx->text.right_column;
#endif
    }
    else {	/* == XawFmt8Bit */
	text.ptr = ptr = XawStackAlloc(text.length * mult, ptrbuf);
	for (count = 0; count < mult; count++) {
	    strncpy(ptr, strbuf, text.length);
	    ptr += text.length;
	}
#ifndef OLDXAW
	if (mult == 1)
	    format = ctx->text.left_column < ctx->text.right_column;
#endif
    }

    text.length = text.length * mult;
    text.firstPos = 0;

    StartAction(ctx, event);
#ifndef OLDXAW
    if (mult == 1)
	_XawSourceSetUndoMerge((TextSrcObject)ctx->text.source, True);
#endif

    from = ctx->text.insertPos;
#ifndef OLDXAW
    if (ctx->text.overwrite) {
	XawTextPosition tmp;

	to = from + mult;
	tmp = SrcScan(ctx->text.source, from, XawstEOL, XawsdRight, 1, False);
	if (to > tmp)
	    to = tmp;
    }
    else
#endif
	to = from;

    error = _XawTextReplace(ctx, from , to, &text);

    if (error == XawEditDone) {
	ctx->text.from_left = -1;
	ctx->text.insertPos = SrcScan(ctx->text.source, ctx->text.old_insert,
				      XawstPositions, XawsdRight,
				      text.length, True);
	if (ctx->text.auto_fill) {
#ifndef OLDXAW
	    if (format)
		(void)FormatText(ctx, SrcScan(ctx->text.source,
					      ctx->text.insertPos, XawstEOL,
					      XawsdLeft, 1, False), False,
					      NULL, 0);
	    else
#endif
		AutoFill(ctx);
	}
    }
    else
	XBell(XtDisplay(ctx), 50);

    XawStackFree(text.ptr, ptrbuf);
    EndAction(ctx);

    if (error == XawEditDone && text.format == XawFmt8Bit && text.length == 1
	&& (text.ptr[0] == ')' || text.ptr[0] == ']' || text.ptr[0] == '}')
	&& ctx->text.display_caret) {
	static struct timeval tmval = {0, 500000};
	fd_set fds;
	Widget source = ctx->text.source;
	XawTextPosition insertPos = ctx->text.insertPos, pos, tmp, last;
	char left, right = text.ptr[0];
	int level = 0;
	XtAppContext app_context = XtWidgetToApplicationContext(w);

	left = right == ')' ? '(' : right == ']' ? '[' : '{';

	last = insertPos - 1;
	do {
	    text.ptr[0] = left;
	    pos = XawTextSourceSearch(source, last, XawsdLeft, &text);
	    if (pos == XawTextSearchError || !IsPositionVisible(ctx, pos))
		return;
	    text.ptr[0] = right;
	    tmp = pos;
	    do {
		tmp = XawTextSourceSearch(source, tmp, XawsdRight, &text);
		if (tmp == XawTextSearchError)
		    return;
		if (tmp <= last)
		    ++level;
	    } while (++tmp <= last);
	    --level;
	    last = pos;
	} while (level);

	StartAction(ctx, NULL);
#ifndef OLDXAW
	_XawSourceSetUndoMerge((TextSrcObject)ctx->text.source, True);
#endif
	ctx->text.insertPos = pos;
	EndAction(ctx);

	XSync(XtDisplay(w), False);
	while (XtAppPending(app_context) & XtIMXEvent) {
	    XEvent ev;
	    if (! XtAppPeekEvent(app_context, &ev))
		break;
	    if (ev.type == KeyPress || ev.type == ButtonPress)
		break;
	    XtAppProcessEvent(app_context, XtIMXEvent);
	}
	FD_ZERO(&fds);
	FD_SET(ConnectionNumber(XtDisplay(w)), &fds);
	(void)select(FD_SETSIZE, &fds, NULL, NULL, &tmval);
	if (tmval.tv_usec != 500000)
	    usleep(40000);

	StartAction(ctx, NULL);
#ifndef OLDXAW
	_XawSourceSetUndoMerge((TextSrcObject)ctx->text.source, True);
#endif
	ctx->text.insertPos = insertPos;
	EndAction(ctx);
    }
}

/* IfHexConvertHexElseReturnParam() - called by InsertString
 *
 * i18n requires the ability to specify multiple characters in a hexa-
 * decimal string at once.  Since Insert was already too long, I made
 * this a seperate routine.
 *
 * A legal hex string in MBNF: '0' 'x' ( HEX-DIGIT HEX-DIGIT )+ '\0'
 *
 * WHEN:    the passed param is a legal hex string
 * RETURNS: a pointer to that converted, null terminated hex string;
 *	    len_return holds the character count of conversion result
 *
 * WHEN:    the passed param is not a legal hex string:
 * RETURNS: the parameter passed;
 *	    len_return holds the char count of param.
 *
 * NOTE:    In neither case will there be strings to free. */
static char *
IfHexConvertHexElseReturnParam(char *param, int *len_return)
{
    char *p;		/* steps through param char by char */
    char c;		/* holds the character pointed to by p */
    int ind;		/* steps through hexval buffer char by char */
    static char hexval[XawTextActionMaxHexChars];
    Boolean first_digit;

    /* reject if it doesn't begin with 0x and at least one more character. */
    if ((param[0] != '0') || (param[1] != 'x') || (param[2] == '\0')) {
	*len_return = strlen(param);
	return(param);
    }

    /* Skip the 0x; go character by character shifting and adding. */
    first_digit = True;
    ind = 0;
    hexval[ind] = '\0';

    for (p = param+2; (c = *p) != '\0'; p++) {
	hexval[ind] *= 16;
	if (c >= '0' && c <= '9')
	    hexval[ind] += c - '0';
	else if (c >= 'a' && c <= 'f')
	    hexval[ind] += c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
	    hexval[ind] += c - 'A' + 10;
	else
	    break;

	/* If we didn't break in preceding line, it was a good hex char. */
	if (first_digit)
	    first_digit = False;
	else {
	    first_digit = True;
	    if (++ind < XawTextActionMaxHexChars)
		hexval[ind] = '\0';
	    else {
		*len_return = strlen(param);
		return(param);
	    }
	}
    }

    /* We quit the above loop becasue we hit a non hex.  If that char is \0... */
    if ((c == '\0') && first_digit) {
	*len_return = strlen(hexval);
	return (hexval);       /* ...it was a legal hex string, so return it */
    }

    /* Else, there were non-hex chars or odd digit count, so... */

    *len_return = strlen(param);
    return (param);			   /* ...return the verbatim string. */
}

/* InsertString() - action
 *
 * Mostly rewritten for R6 i18n.
 *
 * Each parameter, in turn, will be insert at the inputPos
 * and the inputPos advances to the insertion's end.
 *
 * The exception is that parameters composed of the two
 * characters 0x, followed only by an even number of
 * hexadecimal digits will be converted to characters */
/*ARGSUSED*/
static void
InsertString(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    XtAppContext app_con = XtWidgetToApplicationContext(w);
    XawTextBlock text;
    int i;

    text.firstPos = 0;
    text.format = _XawTextFormat(ctx);

    StartAction(ctx, event);
    for (i = *num_params; i; i--, params++) {	/* DO FOR EACH PARAMETER */
	text.ptr = IfHexConvertHexElseReturnParam(*params, &text.length);

	if (text.length == 0)
	    continue;

	if (XawTextFormat(ctx, XawFmtWide)) {	/* convert to WC */
	    int temp_len;

	    text.ptr = (char*)_XawTextMBToWC(XtDisplay(w), text.ptr,
					     &text.length);

	    if (text.ptr == NULL) {	  /* conversion error */
		XtAppWarningMsg(app_con,
				"insertString", "textAction", "XawError",
				"insert-string()'s parameter contents "
				"not legal in this locale.",
				NULL, NULL);
		ParameterError(w, *params);
		continue;
	   }

	    /* Double check that the new input is legal: try to convert to MB. */

	    temp_len = text.length;	 /* _XawTextWCToMB's 3rd arg is in_out */
	    if (_XawTextWCToMB(XtDisplay(w), (wchar_t*)text.ptr, &temp_len)
		== NULL) {
		XtAppWarningMsg( app_con,
				 "insertString", "textAction", "XawError",
				 "insert-string()'s parameter contents "
				 "not legal in this locale.",
				 NULL, NULL);
		ParameterError(w, *params);
		continue;
	    }
	} /* convert to WC */

	if (_XawTextReplace(ctx, ctx->text.insertPos,
			    ctx->text.insertPos, &text)) {
	    XBell(XtDisplay(ctx), 50);
	    EndAction(ctx);
	    return;
	}

	ctx->text.from_left = -1;
	/* Advance insertPos to the end of the string we just inserted. */
	ctx->text.insertPos = SrcScan(ctx->text.source, ctx->text.old_insert,
				       XawstPositions, XawsdRight, text.length,
				      True);

    } /* DO FOR EACH PARAMETER */

    EndAction(ctx);
}

/* DisplayCaret() - action
 *
 * The parameter list should contain one boolean value.  If the
 * argument is true, the cursor will be displayed.  If false, not.
 *
 * The exception is that EnterNotify and LeaveNotify events may
 * have a second argument, "always".  If they do not, the cursor
 * is only affected if the focus member of the event is true.	*/
static void
DisplayCaret(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    Bool display_caret = True;

    if	((event->type == EnterNotify || event->type == LeaveNotify)
	 && ((*num_params >= 2) && (strcmp(params[1], "always") == 0))
	 && (!event->xcrossing.focus))
	return;

    if (*num_params > 0) {	/* default arg is "True" */
	XrmValue from, to;
	from.size = strlen(from.addr = params[0]);
	XtConvert(w, XtRString, &from, XtRBoolean, &to);

	if (to.addr != NULL)
	    display_caret = *(Boolean*)to.addr;
	if (ctx->text.display_caret == display_caret)
	    return;
    }
    StartAction(ctx, event);
    ctx->text.display_caret = display_caret;
    EndAction(ctx);
}

#ifndef OLDXAW
static void
Numeric(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

    if (ctx->text.numeric) {
	long mult = ctx->text.mult;

	if (*num_params != 1 || strlen(params[0]) != 1
	    || (!isdigit(params[0][0])
		&& (params[0][0] != '-' || mult != 0))) {
	    char err_buf[256];

	    if (event && (event->type == KeyPress || event->type == KeyRelease)
		&& params[0][0] == '-') {
		InsertChar(w, event, params, num_params);
		return;
	    }
	    snprintf(err_buf, sizeof(err_buf),
		     "numeric: Invalid argument%s'%s'",
		     *num_params ? ", " : "",
		     *num_params ? params[0] : "");
	    XtAppWarning(XtWidgetToApplicationContext(w), err_buf);
	    ctx->text.numeric = False;
	    ctx->text.mult = 1;
	    return;
	}
	if (params[0][0] == '-') {
	    ctx->text.mult = 32767;
	    return;
	}
	else if (mult == 32767) {
	    mult = ctx->text.mult = - (params[0][0] - '0');
	    return;
	}
	else {
	    mult = mult * 10 + (params[0][0] - '0') * (mult < 0 ? -1 : 1);
	    ctx->text.mult = ctx->text.mult * 10 + (params[0][0] - '0') *
			     (mult < 0 ? -1 : 1);
	}
	if (mult != ctx->text.mult || mult >= 32767) {	/* checks for overflow */
	    XBell(XtDisplay(w), 0);
	    ctx->text.mult = 1;
	    ctx->text.numeric = False;
	    return;
	}
    }
    else
	InsertChar(w, event, params, num_params);
}

/*ARGSUSED*/
static void
KeyboardReset(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;

    ctx->text.numeric = False;
    ctx->text.mult = 1;

    (void)_XawTextSrcToggleUndo((TextSrcObject)ctx->text.source);

    if (ctx->text.kill_ring_ptr) {
	--ctx->text.kill_ring_ptr->refcount;
	ctx->text.kill_ring_ptr = NULL;
    }
    ctx->text.kill_ring = 0;

    XBell(XtDisplay(w), 0);
}
#endif /* OLDXAW */

/* Multiply() - action
 *
 * The parameter list may contain either a number or the string 'Reset'.
 *
 * A number will multiply the current multiplication factor by that number.
 * Many of the text widget actions will will perform n actions, where n is
 * the multiplication factor.
 *
 * The string reset will reset the mutiplication factor to 1. */
/*ARGSUSED*/
static void
Multiply(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    int mult;

    if (*num_params != 1) {
	XtAppError(XtWidgetToApplicationContext(w),
		   "Xaw Text Widget: multiply() takes exactly one argument.");
	XBell(XtDisplay(w), 0);
	return;
    }

    if ((params[0][0] == 'r') || (params[0][0] == 'R')) {
	XBell(XtDisplay(w), 0);
#ifndef OLDXAW
	ctx->text.numeric = False;
#endif
	ctx->text.mult = 1;
	return;
    }

#ifndef OLDXAW
    if (params[0][0] == 's' || params[0][0] == 'S') {
	ctx->text.numeric = True;
	ctx->text.mult = 0;
	return;
    }
    else
#endif
	if ((mult = atoi(params[0])) == 0) {
	char buf[BUFSIZ];

	snprintf(buf, sizeof(buf),
		 "Xaw Text Widget: multiply() argument "
		 "must be a number greater than zero, or 'Reset'.");
	XtAppError(XtWidgetToApplicationContext(w), buf);
	XBell(XtDisplay(w), 50);
	return;
    }

    ctx->text.mult *= mult;
}

/* StripOutOldCRs() - called from FormRegion
 *
 * removes CRs in widget ctx, from from to to.
 *
 * RETURNS: the new ending location (we may add some characters),
 * or XawReplaceError if the widget can't be written to. */
static XawTextPosition
StripOutOldCRs(TextWidget ctx, XawTextPosition from, XawTextPosition to,
	       XawTextPosition *pos, int num_pos)
{
    XawTextPosition startPos, endPos, eop_begin, eop_end, temp;
    Widget src = ctx->text.source;
    XawTextBlock text;
    char *buf;
    static wchar_t wc_two_spaces[3];
    int idx;

    /* Initialize our TextBlock with two spaces. */
    text.firstPos = 0;
    text.format = _XawTextFormat(ctx);
    if (text.format == XawFmt8Bit)
      text.ptr= "  ";
    else {
	wc_two_spaces[0] = _Xaw_atowc(XawSP);
	wc_two_spaces[1] = _Xaw_atowc(XawSP);
	wc_two_spaces[2] = 0;
	text.ptr = (char*)wc_two_spaces;
    }

    /* Strip out CR's. */
    eop_begin = eop_end = startPos = endPos = from;

    /* CONSTCOND */
    while (TRUE) {
	endPos=SrcScan(src, startPos, XawstEOL, XawsdRight, 1, False);

	temp = SrcScan(src, endPos, XawstWhiteSpace, XawsdLeft, 1, False);
	temp = SrcScan(src, temp,   XawstWhiteSpace, XawsdRight,1, False);

	if (temp > startPos)
	    endPos = temp;

	if (endPos >= to)
	    break;

	if (endPos >= eop_begin) {
	    startPos = eop_end;
	    eop_begin=SrcScan(src, startPos, XawstParagraph,
			      XawsdRight, 1,False);
	    eop_end = SrcScan(src, startPos, XawstParagraph,
			      XawsdRight, 1, True);
	}
	else {
	    XawTextPosition periodPos, next_word;
	    int i, len;

	    periodPos = SrcScan(src, endPos, XawstPositions,
				XawsdLeft, 1, True);
	    next_word = SrcScan(src, endPos, XawstWhiteSpace,
				XawsdRight, 1, False);

	    len = next_word - periodPos;

	    text.length = 1;
	    buf = _XawTextGetText(ctx, periodPos, next_word);
	    if (text.format == XawFmtWide) {
		if (periodPos < endPos && ((wchar_t*)buf)[0] == _Xaw_atowc('.'))
		  text.length++;
	    }
	    else
		if (periodPos < endPos && buf[0] == '.')
		    text.length++;	  /* Put in two spaces. */

	    /*
	     * Remove all extra spaces.
	     */
	    for (i = 1 ; i < len; i++)
		if (text.format ==  XawFmtWide) {
		    if (!iswspace(((wchar_t*)buf)[i]) || ((periodPos + i) >= to))
			break;
		}
		else if (!isspace(buf[i]) || (periodPos + i) >= to)
		    break;

	    XtFree(buf);

	    to -= (i - text.length - 1);
	    startPos = SrcScan(src, periodPos, XawstPositions,
			       XawsdRight, i, True);
	    if (_XawTextReplace(ctx, endPos, startPos, &text) != XawEditDone)
		return (XawReplaceError);

	    for (idx = 0; idx < num_pos; idx++) {
		if (endPos < pos[idx]) {
		    if (startPos < pos[idx])
			pos[idx] -= startPos - endPos;
		    else
			pos[idx] = endPos;
		    pos[idx] += text.length;
		}
	    }

	    startPos -= i - text.length;
	}
    }

    return (to);
}

/* InsertNewCRs() - called from FormRegion
 *
 * inserts new CRs for FormRegion, thus for FormParagraph action */
static void
InsertNewCRs(TextWidget ctx, XawTextPosition from, XawTextPosition to,
	     XawTextPosition *pos, int num_pos)
{
    XawTextPosition startPos, endPos, space, eol;
    XawTextBlock text;
    int i, width, height, len, wwidth, idx;
    char *buf;
    static wchar_t wide_CR[2];

    text.firstPos = 0;
    text.length = 1;
    text.format = _XawTextFormat(ctx);

    if (text.format == XawFmt8Bit)
	text.ptr = "\n";
    else {
	wide_CR[0] = _Xaw_atowc(XawLF);
	wide_CR[1] = 0;
	text.ptr = (char*)wide_CR;
    }

    startPos = from;

    wwidth = (int)XtWidth(ctx) - (int)HMargins(ctx);
    if (ctx->text.wrap != XawtextWrapNever) {
	XRectangle cursor;

	XawTextSinkGetCursorBounds(ctx->text.sink, &cursor);
	wwidth -= (int)cursor.width;
    }
    wwidth = XawMax(0, wwidth);

    /* CONSTCOND */
    while (TRUE) {
	XawTextSinkFindPosition(ctx->text.sink, startPos,
				(int)ctx->text.r_margin.left, wwidth,
				True, &eol, &width, &height);
	if (eol == startPos)
	    ++eol;
	if (eol >= to)
	    break;

	eol = SrcScan(ctx->text.source, eol, XawstPositions,
		      XawsdLeft, 1, True);
	space = SrcScan(ctx->text.source, eol, XawstWhiteSpace,
			XawsdRight,1, True);

	startPos = endPos = eol;
	if (eol == space)
	    return;

	len = (int)(space - eol);
	buf = _XawTextGetText(ctx, eol, space);
	for (i = 0 ; i < len ; i++)
	    if (text.format == XawFmtWide) {
		if (!iswspace(((wchar_t*)buf)[i]))
		    break;
	    }
	    else if (!isspace(buf[i]))
		break;

	to -= (i - 1);
	endPos = SrcScan(ctx->text.source, endPos,
			 XawstPositions, XawsdRight, i, True);
	XtFree(buf);

	if (_XawTextReplace(ctx, startPos, endPos, &text))
	    return;

	for (idx = 0; idx < num_pos; idx++) {
	    if (startPos < pos[idx]) {
		if (endPos < pos[idx])
		    pos[idx] -= endPos - startPos;
		else
		    pos[idx] = startPos;
		pos[idx] += text.length;
	    }
	}

	startPos = SrcScan(ctx->text.source, startPos,
			   XawstPositions, XawsdRight, 1, True);
    }
}

/* FormRegion() - called by FormParagraph
 *
 * oversees the work of paragraph-forming a region
 *
 * Return:
 *	XawEditDone if successful, or XawReplaceError
 */
static int
FormRegion(TextWidget ctx, XawTextPosition from, XawTextPosition to,
	   XawTextPosition *pos, int num_pos)
{
#ifndef OLDXAW
    Bool format = ctx->text.auto_fill
	&& ctx->text.left_column < ctx->text.right_column;
#endif

    if (from >= to)
	return (XawEditDone);

#ifndef OLDXAW
    if (format) {
	XawTextPosition len = ctx->text.lastPos;
	int inc = 0;

	if (ctx->text.justify == XawjustifyLeft ||
	    ctx->text.justify == XawjustifyFull) {
	    Untabify(ctx, from, to, pos, num_pos, NULL);
	    to += ctx->text.lastPos - len;
	    len = ctx->text.insertPos;
	    (void)BlankLine((Widget)ctx, from, &inc);
	    if (from + inc >= to)
		return (XawEditDone);
	}
	if (!StripSpaces(ctx, from + inc, to, pos, num_pos, NULL))
	    return (XawReplaceError);
	to += ctx->text.lastPos - len;

	FormatText(ctx, from, ctx->text.justify != XawjustifyFull, pos, num_pos);
    }
    else {
#endif
	if ((to = StripOutOldCRs(ctx, from, to, pos, num_pos)) == XawReplaceError)
	    return (XawReplaceError);
	InsertNewCRs(ctx, from, to, pos, num_pos);
#ifndef OLDXAW
    }
#endif
    ctx->text.from_left = -1;

    return (XawEditDone);
}

#ifndef OLDXAW
static Bool
BlankLine(Widget w, XawTextPosition pos, int *blanks_return)
{
    int i, blanks = 0;
    XawTextBlock block;
    Widget src = XawTextGetSource(w);
    XawTextPosition l = SrcScan(src, pos, XawstEOL, XawsdLeft, 1, False);
    XawTextPosition r = SrcScan(src, pos, XawstEOL, XawsdRight, 1, False);

    while (l < r) {
	l = XawTextSourceRead(src, l, &block, r - l);
	if (block.length == 0) {
	    if (blanks_return)
		*blanks_return = blanks;
	    return (True);
	}
	if (XawTextFormat((TextWidget)w, XawFmt8Bit)) {
	    for (i = 0; i < block.length; i++, blanks++)
		if (block.ptr[i] != ' ' &&
		    block.ptr[i] != '\t') {
		    if (blanks_return)
			*blanks_return = blanks;
		    return (block.ptr[i] == '\n');
		}
	}
	else if (XawTextFormat((TextWidget)w, XawFmtWide)) {
	    for (i = 0; i < block.length; i++, blanks++)
		if (_Xaw_atowc(XawSP) != ((wchar_t*)block.ptr)[i] &&
		    _Xaw_atowc(XawTAB) != ((wchar_t*)block.ptr)[i]) {
		    if (blanks_return)
			*blanks_return = blanks;
		    return (_Xaw_atowc(XawLF) == ((wchar_t*)block.ptr)[i]);
		}
	}
    }

    return (True);
}

static Bool
GetBlockBoundaries(TextWidget ctx,
		   XawTextPosition *from_return, XawTextPosition *to_return)
{
    XawTextPosition from, to;

    if (ctx->text.auto_fill && ctx->text.left_column < ctx->text.right_column) {
	if (ctx->text.s.left != ctx->text.s.right) {
	    from = SrcScan(ctx->text.source,
			   XawMin(ctx->text.s.left, ctx->text.s.right),
			   XawstEOL, XawsdLeft, 1, False);
	    to   = SrcScan(ctx->text.source,
			   XawMax(ctx->text.s.right, ctx->text.s.right),
			   XawstEOL, XawsdRight, 1, False);
	}
	else {
	    XawTextBlock block;
	    XawTextPosition tmp;
	    Bool first;

	    from = to = ctx->text.insertPos;

	    /* find from position */
	    first = True;
	    while (1) {
		tmp = from;
		from = SrcScan(ctx->text.source, from, XawstEOL, XawsdLeft,
			       1 + !first, False);
		XawTextSourceRead(ctx->text.source, from, &block, 1);
		if (block.length == 0 ||
		    (XawTextFormat(ctx, XawFmt8Bit) &&
		     block.ptr[0] != ' ' &&
		     block.ptr[0] != '\t' &&
		     !isalnum(*(unsigned char*)block.ptr)) ||
		    (XawTextFormat(ctx, XawFmtWide) &&
		     _Xaw_atowc(XawSP) != *(wchar_t*)block.ptr &&
		     _Xaw_atowc(XawTAB) != *(wchar_t*)block.ptr &&
		     !iswalnum(*(wchar_t*)block.ptr)) ||
		    BlankLine((Widget)ctx, from, NULL)) {
		    from = tmp;
		    break;
		}
		if (from == tmp && !first)
		    break;
		first = False;
	    }
	    if (first)
		return (False);

	    /* find to position */
	    first = True;
	    while (1) {
		tmp = to;
		to = SrcScan(ctx->text.source, to, XawstEOL, XawsdRight,
			     1 + !first, False);
		XawTextSourceRead(ctx->text.source, to + (to < ctx->text.lastPos),
				  &block, 1);
		if (block.length == 0 ||
		    (XawTextFormat(ctx, XawFmt8Bit) &&
		     block.ptr[0] != ' ' &&
		     block.ptr[0] != '\t' &&
		     !isalnum(*(unsigned char*)block.ptr)) ||
		    (XawTextFormat(ctx, XawFmtWide) &&
		     _Xaw_atowc(XawSP) != *(wchar_t*)block.ptr &&
		     _Xaw_atowc(XawTAB) != *(wchar_t*)block.ptr &&
		     !iswalnum(*(wchar_t*)block.ptr)) ||
		    BlankLine((Widget)ctx, to, NULL))
		    break;
		if (to == tmp && !first)
		    break;
		first = False;
	    }
	}
    }
    else {
	from = SrcScan(ctx->text.source, ctx->text.insertPos, XawstEOL,
		       XawsdLeft, 1, False);
	if (BlankLine((Widget)ctx, from, NULL))
	    return (False);
	from = SrcScan(ctx->text.source, from, XawstParagraph,
		       XawsdLeft, 1, False);
	if (BlankLine((Widget)ctx, from, NULL))
	    from = SrcScan(ctx->text.source, from, XawstEOL,
			   XawsdRight, 1, True);
	to = SrcScan(ctx->text.source, from, XawstParagraph,
			XawsdRight, 1, False);
    }

    if (from < to) {
	*from_return = from;
	*to_return = to;
	return (True);
    }

    return (False);
}
#endif /* OLDXAW */

/* FormParagraph() - action
 *
 * removes and reinserts CRs to maximize line length without clipping */
/*ARGSUSED*/
static void
FormParagraph(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition from, to, buf[32], *pos;
#ifndef OLDXAW
    XawTextPosition endPos = 0;
    char *lbuf = NULL, *rbuf;
    TextSrcObject src = (TextSrcObject)ctx->text.source;
    Cardinal i;
    Bool undo = src->textSrc.enable_undo && src->textSrc.undo_state == False;
#endif

    StartAction(ctx, event);

#ifndef OLDXAW
    pos = XawStackAlloc(sizeof(XawTextPosition) * src->textSrc.num_text, buf);
    for (i = 0; i < src->textSrc.num_text; i++)
	pos[i] = ((TextWidget)src->textSrc.text[i])->text.old_insert;
#else
    pos = buf;
    *pos = ctx->text.old_insert;
#endif

#ifndef OLDXAW
    if (!GetBlockBoundaries(ctx, &from, &to)) {
	EndAction(ctx);
	XawStackFree(pos, buf);
	return;
    }

    if (undo) {
	src->textSrc.undo_state = True;
	lbuf = _XawTextGetText(ctx, from, to);
	endPos = ctx->text.lastPos;
    }

    if (FormRegion(ctx, from, to, pos, src->textSrc.num_text) == XawReplaceError) {
	XawStackFree(pos, buf);
	pos = buf;
#else
    from =  SrcScan(ctx->text.source, ctx->text.insertPos,
		    XawstParagraph, XawsdLeft, 1, False);
    to  =   SrcScan(ctx->text.source, from,
		    XawstParagraph, XawsdRight, 1, False);

    if (FormRegion(ctx, from, to, pos, 1) == XawReplaceError) {
#endif
	XBell(XtDisplay(w), 0);
#ifndef OLDXAW
	if (undo) {
	    src->textSrc.undo_state = False;
	    XtFree(lbuf);
	}
#endif
    }
#ifndef OLDXAW
    else if (undo) {
	/* makes the form-paragraph only one undo/redo step */
	unsigned llen, rlen, size;
	XawTextBlock block;

	llen = to - from;
	rlen = llen + (ctx->text.lastPos - endPos);

	block.firstPos = 0;
	block.format = _XawTextFormat(ctx);

	rbuf = _XawTextGetText(ctx, from, from + rlen);

	size = XawTextFormat(ctx, XawFmtWide) ? sizeof(wchar_t) : sizeof(char);
	if (llen != rlen || memcmp(lbuf, rbuf, llen * size)) {
	    block.ptr = lbuf;
	    block.length = llen;
	    _XawTextReplace(ctx, from, from + rlen, &block);

	    src->textSrc.undo_state = False;
	    block.ptr = rbuf;
	    block.length = rlen;
	    _XawTextReplace(ctx, from, from + llen, &block);
	}
	else
	    src->textSrc.undo_state = False;
	XtFree(lbuf);
	XtFree(rbuf);
    }

    for (i = 0; i < src->textSrc.num_text; i++) {
	TextWidget tw = (TextWidget)src->textSrc.text[i];

	tw->text.old_insert = tw->text.insertPos = pos[i];
	_XawTextBuildLineTable(tw, SrcScan((Widget)src, tw->text.lt.top, XawstEOL,
			       XawsdLeft, 1, False), False);
	tw->text.clear_to_eol = True;
    }
    XawStackFree(pos, buf);
#else
    ctx->text.old_insert = ctx->text.insertPos = *pos;
    _XawTextBuildLineTable(ctx, SrcScan(ctx->text.source, ctx->text.lt.top,
			   XawstEOL, XawsdLeft, 1, False), False);
    ctx->text.clear_to_eol = True;
#endif
    ctx->text.showposition = True;

    EndAction(ctx);
}

/* TransposeCharacters() - action
 *
 * Swaps the character to the left of the mark
 * with the character to the right of the mark */
/*ARGSUSED*/
static void
TransposeCharacters(Widget w, XEvent *event,
		    String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    XawTextPosition start, end;
    XawTextBlock text;
    char *buf;
    int i, mult = MULT(ctx);

    if (mult < 0) {
	ctx->text.mult = 1;
	return;
    }

    StartAction(ctx, event);

    /* Get bounds. */

    start = SrcScan(ctx->text.source, ctx->text.insertPos, XawstPositions,
		    XawsdLeft, 1, True);
    end = SrcScan(ctx->text.source, ctx->text.insertPos, XawstPositions,
		  XawsdRight, mult, True);

    /* Make sure we aren't at the very beginning or end of the buffer. */

    if (start == ctx->text.insertPos || end == ctx->text.insertPos) {
	XBell(XtDisplay(w), 0);   /* complain. */
	EndAction(ctx);
	return;
    }

    ctx->text.from_left = -1;
    ctx->text.insertPos = end;

    text.firstPos = 0;
    text.format = _XawTextFormat(ctx);

    /* Retrieve text and swap the characters. */
    if (text.format == XawFmtWide) {
	wchar_t wc;
	wchar_t *wbuf;

	wbuf = (wchar_t*)_XawTextGetText(ctx, start, end);
	text.length = wcslen(wbuf);
	wc = wbuf[0];
	for (i = 1; i < text.length; i++)
	    wbuf[i - 1] = wbuf[i];
	wbuf[i - 1] = wc;
	buf = (char*)wbuf; /* so that it gets assigned and freed */
    }
    else {	/* thus text.format == XawFmt8Bit */
	char c;

	buf = _XawTextGetText(ctx, start, end);
	text.length = strlen(buf);
	c = buf[0];
	for (i = 1; i < text.length; i++)
	    buf[i - 1] = buf[i];
	buf[i - 1] = c;
    }

    text.ptr = buf;

    /* Store new text in source. */

    if (_XawTextReplace (ctx, start, end, &text))
	XBell(XtDisplay(w), 0);
    XtFree((char *)buf);
    EndAction(ctx);
}

#ifndef OLDXAW
/*ARGSUSED*/
static void
Undo(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    TextWidget ctx = (TextWidget)w;
    int mul = MULT(ctx);
    Bool toggle = False;

    if (mul < 0) {
	toggle = True;
	_XawTextSrcToggleUndo((TextSrcObject)ctx->text.source);
	ctx->text.mult = mul = -mul;
    }

    StartAction(ctx, event);
    for (; mul; --mul)
	if (!_XawTextSrcUndo((TextSrcObject)ctx->text.source, &ctx->text.insertPos))
	    break;
    ctx->text.showposition = True;

    if (toggle)
	_XawTextSrcToggleUndo((TextSrcObject)ctx->text.source);
    EndAction(ctx);
}
#endif

/* NoOp() - action
 * This action performs no action, and allows the user or
 * application programmer to unbind a translation.
 *
 * Note: If the parameter list contains the string "RingBell" then
 *	 this action will ring the bell.
 */
/*ARGSUSED*/
static void
NoOp(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    if (*num_params != 1)
	return;

    switch(params[0][0]) {
	case 'R':
	case 'r':
	    XBell(XtDisplay(w), 0);
	    /*FALLTROUGH*/
	default:
	    break;
    }
}

/* Reconnect() - action
 * This reconnects to the input method.  The user will typically call
 * this action if/when connection has been severed, or when the app
 * was started up before an IM was started up
 */
/*ARGSUSED*/
static void
Reconnect(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    _XawImReconnect(w);
}

#define	CAPITALIZE	1
#define	DOWNCASE	2
#define UPCASE		3

#ifdef NO_LIBC_I18N
static int
ToLower(int ch)
{
    char buf[2];

    *buf = ch;
    XmuNCopyISOLatin1Lowered(buf, buf, sizeof(buf));

    return (*buf);
}

static int
ToUpper(int ch)
{
    char buf[2];

    *buf = ch;
    XmuNCopyISOLatin1Uppered(buf, buf, sizeof(buf));

    return (*buf);
}

static int
IsAlnum(int ch)
{
    return ((ch >= '0' && ch <= '9') || ToUpper(ch) != ch || ToLower(ch) != ch);
}

static int
IsLower(int ch)
{
    char upbuf[2];
    char lobuf[2];

    *upbuf = *lobuf = ch;
    XmuNCopyISOLatin1Lowered(lobuf, lobuf, sizeof(lobuf));
    XmuNCopyISOLatin1Uppered(upbuf, upbuf, sizeof(upbuf));

    return (*lobuf != *upbuf && ch == *lobuf);
}

static int
IsUpper(int ch)
{
    char upbuf[2];
    char lobuf[2];

    *upbuf = *lobuf = ch;
    XmuNCopyISOLatin1Lowered(lobuf, lobuf, sizeof(lobuf));
    XmuNCopyISOLatin1Uppered(upbuf, upbuf, sizeof(upbuf));

    return (*lobuf != *upbuf && ch == *upbuf);
}
#else
#define	ToLower	tolower
#define ToUpper	toupper
#define IsAlnum isalnum
#define IsLower islower
#define IsUpper isupper
#endif

static void
CaseProc(Widget w, XEvent *event, int cmd)
{
    TextWidget ctx = (TextWidget)w;
    short mul = MULT(ctx);
    XawTextPosition left, right;
    XawTextBlock block;
    Bool changed = False;
    unsigned char ch, mb[sizeof(wchar_t)];
    int i, count;

    if (mul > 0)
	right = SrcScan(ctx->text.source, left = ctx->text.insertPos,
			XawstAlphaNumeric, XawsdRight, mul, False);
    else
	left = SrcScan(ctx->text.source, right = ctx->text.insertPos,
		       XawstAlphaNumeric, XawsdLeft, 1 + -mul, False);
    block.firstPos = 0;
    block.format = _XawTextFormat(ctx);
    block.length = right - left;
    block.ptr = _XawTextGetText(ctx, left, right);

    count = 0;
    if (block.format == XawFmt8Bit)
	for (i = 0; i < block.length; i++) {
	    if (!IsAlnum(*mb = (unsigned char)block.ptr[i]))
		count = 0;
	    else if (++count == 1 || cmd != CAPITALIZE) {
		ch = cmd == DOWNCASE ? ToLower(*mb) : ToUpper(*mb);
		if (ch != *mb) {
		    changed = True;
		    block.ptr[i] = ch;
		}
	    }
	    else if (cmd == CAPITALIZE) {
		if ((ch = ToLower(*mb)) != *mb) {
		    changed = True;
		    block.ptr[i] = ch;
		}
	    }
	}
    else
	for (i = 0; i < block.length; i++) {
	    wctomb((char*)mb, ((wchar_t*)block.ptr)[i]);
	    if (!IsAlnum(*mb))
		count = 0;
	    else if (++count == 1 || cmd != CAPITALIZE) {
		ch = cmd == DOWNCASE ? ToLower(*mb) : ToUpper(*mb);
		if (ch != *mb) {
		    changed = True;
		    ((wchar_t*)block.ptr)[i] = _Xaw_atowc(ch);
		}
	    }
	    else if (cmd == CAPITALIZE) {
		if ((ch = ToLower(*mb)) != *mb) {
		    changed = True;
		    ((wchar_t*)block.ptr)[i] = _Xaw_atowc(ch);
		}
	    }
	}

    StartAction(ctx, event);
    if (changed && _XawTextReplace(ctx, left, right, &block) != XawEditDone)
	XBell(XtDisplay(ctx), 0);
    ctx->text.insertPos = right;
    EndAction(ctx);

    XtFree(block.ptr);
}

/*ARGSUSED*/
static void
CapitalizeWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    CaseProc(w, event, CAPITALIZE);
}

/*ARGSUSED*/
static void
DowncaseWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    CaseProc(w, event, DOWNCASE);
}

/*ARGSUSED*/
static void
UpcaseWord(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    CaseProc(w, event, UPCASE);
}
#undef CAPITALIZE
#undef DOWNCASE
#undef UPCASE

XtActionsRec _XawTextActionsTable[] = {
  /* motion */
  {"forward-character",		MoveForwardChar},
  {"backward-character",	MoveBackwardChar},
  {"forward-word",		MoveForwardWord},
  {"backward-word",		MoveBackwardWord},
  {"forward-paragraph",		MoveForwardParagraph},
  {"backward-paragraph",	MoveBackwardParagraph},
  {"beginning-of-line",		MoveToLineStart},
  {"end-of-line",		MoveToLineEnd},
  {"next-line",			MoveNextLine},
  {"previous-line",		MovePreviousLine},
  {"next-page",			MoveNextPage},
  {"previous-page",		MovePreviousPage},
  {"beginning-of-file",		MoveBeginningOfFile},
  {"end-of-file",		MoveEndOfFile},
  {"scroll-one-line-up",	ScrollOneLineUp},
  {"scroll-one-line-down",	ScrollOneLineDown},

  /* delete */
  {"delete-next-character",	DeleteForwardChar},
  {"delete-previous-character",	DeleteBackwardChar},
  {"delete-next-word",		DeleteForwardWord},
  {"delete-previous-word",	DeleteBackwardWord},
  {"delete-selection",		DeleteCurrentSelection},
  {"delete",			Delete},

  /* kill */
  {"kill-word",			KillForwardWord},
  {"backward-kill-word",	KillBackwardWord},
  {"kill-selection",		KillCurrentSelection},
  {"kill-to-end-of-line",	KillToEndOfLine},
  {"kill-to-end-of-paragraph",	KillToEndOfParagraph},

  /* new line */
  {"newline-and-indent",	InsertNewLineAndIndent},
  {"newline-and-backup",	InsertNewLineAndBackup},
  {"newline",			InsertNewLine},

  /* selection */
  {"select-word",		SelectWord},
  {"select-all",		SelectAll},
  {"select-start",		SelectStart},
  {"select-adjust",		SelectAdjust},
  {"select-end",		SelectEnd},
  {"select-save",		SelectSave},
  {"extend-start",		ExtendStart},
  {"extend-adjust",		ExtendAdjust},
  {"extend-end", 		ExtendEnd},
  {"insert-selection",		InsertSelection},

  /* miscellaneous */
  {"redraw-display",		RedrawDisplay},
  {"insert-file",		_XawTextInsertFile},
  {"search",			_XawTextSearch},
  {"insert-char",		InsertChar},
  {"insert-string",		InsertString},
  {"focus-in",			TextFocusIn},
  {"focus-out",			TextFocusOut},
  {"enter-window",		TextEnterWindow},
  {"leave-window",		TextLeaveWindow},
  {"display-caret",		DisplayCaret},
  {"multiply",			Multiply},
  {"form-paragraph",		FormParagraph},
  {"transpose-characters",	TransposeCharacters},
  {"set-keyboard-focus",	SetKeyboardFocus},
#ifndef OLDXAW
  {"numeric",			Numeric},
  {"undo",			Undo},
  {"keyboard-reset",		KeyboardReset},
  {"kill-ring-yank",		KillRingYank},
  {"toggle-overwrite",		ToggleOverwrite},
  {"indent",			Indent},
#endif
  {"no-op",			NoOp},

  /* case transformations */
  {"capitalize-word",		CapitalizeWord},
  {"downcase-word",		DowncaseWord},
  {"upcase-word",		UpcaseWord},

  /* action to bind translations for text dialogs */
  {"InsertFileAction",		_XawTextInsertFileAction},
  {"DoSearchAction",		_XawTextDoSearchAction},
  {"DoReplaceAction",		_XawTextDoReplaceAction},
  {"SetField",			_XawTextSetField},
  {"PopdownSearchAction",	_XawTextPopdownSearchAction},

  /* reconnect to Input Method */
  {"reconnect-im",		Reconnect} /* Li Yuhong, Omron KK, 1991 */
};

Cardinal _XawTextActionsTableCount = XtNumber(_XawTextActionsTable);
