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
 * Author:  Chris Peterson, MIT X Consortium.
 * Much code taken from X11R3 String and Disk Sources.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <ctype.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xfuncs.h>
#include <X11/Xutil.h>
#include <X11/Xmu/Atoms.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xaw/TextSrcP.h>
#include <X11/Xaw/XawInit.h>
#include "XawI18n.h"
#include "Private.h"

#ifndef OLDXAW
#define UNDO_DEPTH	16384

#define ANCHORS_DIST	4096	/* default distance between anchors */

/*
 * Types
 */
typedef struct {
    XawTextPosition position;
    char *buffer;
    unsigned length;
    unsigned refcount;
    unsigned long format;
} XawTextUndoBuffer;

typedef struct _XawTextUndoList XawTextUndoList;
struct _XawTextUndoList {
    XawTextUndoBuffer *left, *right;
    XawTextUndoList *undo, *redo;
};

struct _XawTextUndo {
    XawTextUndoBuffer **undo;
    unsigned num_undo;
    XawTextUndoList *list, *pointer, *end_mark, *head;
    unsigned num_list;
    XawTextScanDirection dir;
    XawTextUndoBuffer *l_save, *r_save;
    XawTextUndoList *u_save;
    XawTextUndoBuffer *l_no_change, *r_no_change;
    int merge;
    int erase;		/* there are two types of erases */
};
#endif	/* OLDXAW */

/*
 * Class Methods
 */
static Boolean ConvertSelection(Widget, Atom*, Atom*, Atom*, XtPointer*,
				unsigned long*, int*);
static XawTextPosition Read(Widget, XawTextPosition, XawTextBlock*, int);
static int  Replace(Widget, XawTextPosition, XawTextPosition, XawTextBlock*);
static  XawTextPosition Scan(Widget, XawTextPosition, XawTextScanType,
			      XawTextScanDirection, int, Bool);
static XawTextPosition Search(Widget, XawTextPosition, XawTextScanDirection,
			      XawTextBlock*);
static void SetSelection(Widget, XawTextPosition, XawTextPosition, Atom);
static void XawTextSrcClassInitialize(void);
static void XawTextSrcClassPartInitialize(WidgetClass);
static void XawTextSrcInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawTextSrcDestroy(Widget);
static Boolean XawTextSrcSetValues(Widget, Widget, Widget, ArgList, Cardinal*);
/*
 * Prototypes
 */
static void CvtStringToEditMode(XrmValuePtr, Cardinal*,
				 XrmValuePtr, XrmValuePtr);
static Boolean CvtEditModeToString(Display*, XrmValuePtr, Cardinal*,
				   XrmValuePtr, XrmValuePtr, XtPointer*);
#ifndef OLDXAW
static void FreeUndoBuffer(XawTextUndo*);
static void UndoGC(XawTextUndo*);
static void TellSourceChanged(TextSrcObject, XawTextPosition, XawTextPosition,
			      XawTextBlock*, int);
Bool _XawTextSrcUndo(TextSrcObject, XawTextPosition*);
Bool _XawTextSrcToggleUndo(TextSrcObject);
XawTextAnchor *_XawTextSourceFindAnchor(Widget, XawTextPosition);

/*
 * External
 */
void _XawSourceAddText(Widget, Widget);
void _XawSourceRemoveText(Widget, Widget, Bool);
Bool _XawTextSourceNewLineAtEOF(Widget);
void _XawSourceSetUndoErase(TextSrcObject, int);
void _XawSourceSetUndoMerge(TextSrcObject, Bool);
#endif /* OLDXAW */

/*
 * Defined in Text.c
 */
char *_XawTextGetText(TextWidget, XawTextPosition, XawTextPosition);
void _XawTextSourceChanged(Widget, XawTextPosition, XawTextPosition,
			   XawTextBlock*, int);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(TextSrcRec, textSrc.field)
static XtResource resources[] = {
  {
    XtNeditType,
    XtCEditType,
    XtREditMode,
    sizeof(XawTextEditType),
    offset(edit_mode),
    XtRString,
    (XtPointer)"read"
  },
#ifndef OLDXAW
  {
    XtNcallback,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(callback),
    XtRCallback,
    NULL
  },
  {
    XtNsourceChanged,
    XtCChanged,
    XtRBoolean,
    sizeof(Boolean),
    offset(changed),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNenableUndo,
    XtCUndo,
    XtRBoolean,
    sizeof(Boolean),
    offset(enable_undo),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNpropertyCallback,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(property_callback),
    XtRCallback,
    NULL
  },
#endif /* OLDXAW */
};
#undef offset

#define Superclass	(&objectClassRec)
TextSrcClassRec textSrcClassRec = {
  /* object */
  {
    (WidgetClass)Superclass,		/* superclass */
    "TextSrc",				/* class_name */
    sizeof(TextSrcRec),			/* widget_size */
    XawTextSrcClassInitialize,		/* class_initialize */
    XawTextSrcClassPartInitialize,	/* class_part_initialize */
    False,				/* class_inited */
    XawTextSrcInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    NULL,				/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    False,				/* compress_motion */
    False,				/* compress_exposure */
    False,				/* compress_enterleave */
    False,				/* visible_interest */
    XawTextSrcDestroy,			/* destroy */
    NULL,				/* resize */
    NULL,				/* expose */
    XawTextSrcSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    NULL,				/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    NULL,				/* query_geometry */
    NULL,				/* display_accelerator */
    NULL,				/* extension */
  },
  /* text_src */
  {
    Read,				/* Read */
    Replace,				/* Replace */
    Scan,				/* Scan */
    Search,				/* Search */
    SetSelection,			/* SetSelection */
    ConvertSelection,			/* ConvertSelection */
  },
};

WidgetClass textSrcObjectClass = (WidgetClass)&textSrcClassRec;

static XrmQuark QRead, QAppend, QEdit;
#ifndef OLDXAW
static char *SrcNL = "\n";
static wchar_t SrcWNL[2];
#endif

/*
 * Implementation
 */
static void
XawTextSrcClassInitialize(void)
{
    XawInitializeWidgetSet();

#ifndef OLDXAW
    SrcWNL[0] = _Xaw_atowc(XawLF);
    SrcWNL[1] = 0;
#endif
    QRead   = XrmPermStringToQuark(XtEtextRead);
    QAppend = XrmPermStringToQuark(XtEtextAppend);
    QEdit   = XrmPermStringToQuark(XtEtextEdit);
    XtAddConverter(XtRString, XtREditMode,   CvtStringToEditMode,   NULL, 0);
    XtSetTypeConverter(XtREditMode, XtRString, CvtEditModeToString, NULL, 0,
		       XtCacheNone, NULL);
}

static void
XawTextSrcClassPartInitialize(WidgetClass wc)
{
    TextSrcObjectClass t_src, superC;

    t_src = (TextSrcObjectClass)wc;
    superC = (TextSrcObjectClass)t_src->object_class.superclass;

    /*
     * We don't need to check for null super since we'll get to TextSrc
     * eventually
     */
    if (t_src->textSrc_class.Read == XtInheritRead)
	t_src->textSrc_class.Read = superC->textSrc_class.Read;

    if (t_src->textSrc_class.Replace == XtInheritReplace)
	t_src->textSrc_class.Replace = superC->textSrc_class.Replace;

    if (t_src->textSrc_class.Scan == XtInheritScan)
	t_src->textSrc_class.Scan = superC->textSrc_class.Scan;

    if (t_src->textSrc_class.Search == XtInheritSearch)
	t_src->textSrc_class.Search = superC->textSrc_class.Search;

    if (t_src->textSrc_class.SetSelection == XtInheritSetSelection)
	t_src->textSrc_class.SetSelection = superC->textSrc_class.SetSelection;

    if (t_src->textSrc_class.ConvertSelection == XtInheritConvertSelection)
	t_src->textSrc_class.ConvertSelection =
	    superC->textSrc_class.ConvertSelection;
}

/*ARGSUSED*/
static void
XawTextSrcInitialize(Widget request _X_UNUSED, Widget cnew _X_UNUSED,
		     ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
#ifndef OLDXAW
    TextSrcObject src = (TextSrcObject)cnew;

    if (src->textSrc.enable_undo) {
	src->textSrc.undo = (XawTextUndo*)XtCalloc(1, sizeof(XawTextUndo));
	src->textSrc.undo->dir = XawsdLeft;
    }
    else
	src->textSrc.undo = NULL;
    src->textSrc.undo_state = False;
    if (XtIsSubclass(XtParent(cnew), textWidgetClass)) {
	src->textSrc.text = (WidgetList)XtMalloc(sizeof(Widget*));
	src->textSrc.text[0] = XtParent(cnew);
	src->textSrc.num_text = 1;
    }
    else {
	src->textSrc.text = NULL;
	src->textSrc.num_text = 0;
    }

    src->textSrc.anchors = NULL;
    src->textSrc.num_anchors = 0;
    (void)XawTextSourceAddAnchor(cnew, 0);
#endif /* OLDXAW */
}

static void
XawTextSrcDestroy(Widget w _X_UNUSED)
{
#ifndef OLDXAW
    TextSrcObject src = (TextSrcObject)w;

    if (src->textSrc.enable_undo) {
	FreeUndoBuffer(src->textSrc.undo);
	XtFree((char*)src->textSrc.undo);
    }
    XtFree((char*)src->textSrc.text);

    if (src->textSrc.num_anchors) {
	XawTextEntity *entity, *enext;
	int i;

	for (i = 0; i < src->textSrc.num_anchors; i++) {
	    entity = src->textSrc.anchors[i]->entities;
	    while (entity) {
		enext = entity->next;
		XtFree((XtPointer)entity);
		entity = enext;
	    }
	    XtFree((XtPointer)src->textSrc.anchors[i]);
	}
	XtFree((XtPointer)src->textSrc.anchors);
    }
#endif /* OLDXAW */
}

/*ARGSUSED*/
static Boolean
XawTextSrcSetValues(Widget current _X_UNUSED, Widget request _X_UNUSED, Widget cnew _X_UNUSED,
		    ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
#ifndef OLDXAW
    TextSrcObject oldtw = (TextSrcObject)current;
    TextSrcObject newtw = (TextSrcObject)cnew;

    if (oldtw->textSrc.enable_undo != newtw->textSrc.enable_undo) {
	if (newtw->textSrc.enable_undo) {
	    newtw->textSrc.undo = (XawTextUndo*)
		XtCalloc(1, sizeof(XawTextUndo));
	    newtw->textSrc.undo->dir = XawsdLeft;
	}
	else {
	    FreeUndoBuffer(newtw->textSrc.undo);
	    XtFree((char*)newtw->textSrc.undo);
	    newtw->textSrc.undo = NULL;
	}
    }
    if (oldtw->textSrc.changed != newtw->textSrc.changed) {
	if (newtw->textSrc.enable_undo) {
	    if (newtw->textSrc.undo->list) {
		newtw->textSrc.undo->l_no_change =
		    newtw->textSrc.undo->list->left;
		newtw->textSrc.undo->r_no_change =
		    newtw->textSrc.undo->list->right;
	    }
	    else
		newtw->textSrc.undo->l_no_change =
		    newtw->textSrc.undo->r_no_change = NULL;
	}
    }
#endif /* OLDXAW */
    return (False);
}

/*
 * Function:
 *	Read
 *
 * Parameters:
 *	w      - TextSrc Object
 *	pos    - position of the text to retrieve
 *	text   - text block that will contain returned text
 *	length - maximum number of characters to read
 *
 * Description:
 *	This function reads the source.
 *
 * Returns:
 *	The character position following the retrieved text.
 */
/*ARGSUSED*/
static XawTextPosition
Read(Widget w _X_UNUSED, XawTextPosition pos _X_UNUSED, XawTextBlock *text _X_UNUSED, int length _X_UNUSED)
{
    return ((XawTextPosition)0);
}

/*
 * Function:
 *	Replace
 *
 * Parameters:
 *	src	 - Text Source Object
 *	startPos - ends of text that will be removed
 *	endPos	 - ""
 *	text	 - new text to be inserted into buffer at startPos
 *
 * Description:
 *	Replaces a block of text with new text.
 */
/*ARGSUSED*/
static int
Replace(Widget w _X_UNUSED, XawTextPosition startPos _X_UNUSED, XawTextPosition endPos _X_UNUSED,
	XawTextBlock *text _X_UNUSED)
{
    return (XawEditError);
}

/*
 * Function:
 *	Scan
 *
 * Parameters:
 *	w	 - TextSrc Object
 *	position - position to start scanning
 *	type	 - type of thing to scan for
 *	dir	 - direction to scan
 *	count	 - which occurrence 0f this thing to search for
 *	include  - whether or not to include the character found in
 *		   the position that is returned
 *
 * Description:
 *	Scans the text source for the number and type of item specified.
 */
/*ARGSUSED*/
static XawTextPosition
Scan(Widget w _X_UNUSED, XawTextPosition position _X_UNUSED, XawTextScanType type _X_UNUSED,
     XawTextScanDirection dir _X_UNUSED, int count _X_UNUSED, Bool include _X_UNUSED)
{
    return ((XawTextPosition)0);
}

/*
 * Function:
 *	Search
 *
 * Parameters:
 *	w	 - TextSource Object
 *	position - position to start searching
 *	dir	 - direction to search
 *	text	 - the text block to search for
 *
 * Description:
 *	Searches the text source for the text block passed
 */
/*ARGSUSED*/
static XawTextPosition
Search(Widget w _X_UNUSED, XawTextPosition position _X_UNUSED, XawTextScanDirection dir _X_UNUSED,
       XawTextBlock *text _X_UNUSED)
{
    return (XawTextSearchError);
}

/*ARGSUSED*/
static Boolean
ConvertSelection(Widget w _X_UNUSED, Atom *selection _X_UNUSED, Atom *target _X_UNUSED, Atom *type _X_UNUSED,
		 XtPointer *value _X_UNUSED, unsigned long *length _X_UNUSED, int *format _X_UNUSED)
{
    return (False);
}

/*ARGSUSED*/
static void
SetSelection(Widget w _X_UNUSED, XawTextPosition left _X_UNUSED, XawTextPosition right _X_UNUSED,
	     Atom selection _X_UNUSED)
{
}

/*ARGSUSED*/
static void
CvtStringToEditMode(XrmValuePtr args _X_UNUSED, Cardinal *num_args _X_UNUSED,
		    XrmValuePtr fromVal, XrmValuePtr toVal)
{
    static XawTextEditType editType;
    XrmQuark	q;
    char name[7];

    XmuNCopyISOLatin1Lowered(name, (char *)fromVal->addr, sizeof(name));
    q = XrmStringToQuark(name);

    if (q == QRead)
	editType = XawtextRead;
    else if (q == QAppend)
	editType = XawtextAppend;
    else if (q == QEdit)
	editType = XawtextEdit;
    else {
	toVal->size = 0;
	toVal->addr = NULL;
	XtStringConversionWarning((char *)fromVal->addr, XtREditMode);
    }
    toVal->size = sizeof(XawTextEditType);
    toVal->addr = (XPointer)&editType;
}

/*ARGSUSED*/
static Boolean
CvtEditModeToString(Display *dpy, XrmValuePtr args _X_UNUSED, Cardinal *num_args _X_UNUSED,
		    XrmValuePtr fromVal, XrmValuePtr toVal,
		    XtPointer *data _X_UNUSED)
{
    static String buffer;
    Cardinal size;

    switch (*(XawTextEditType *)fromVal->addr) {
	case XawtextRead:
	    buffer = XtEtextRead;
	    break;
	case XawtextAppend:
	    buffer = XtEtextAppend;
	    break;
	case XawtextEdit:
	    buffer = XtEtextEdit;
	    break;
	default:
	    XawTypeToStringWarning(dpy, XtREditMode);
	    toVal->addr = NULL;
	    toVal->size = 0;
	    return (False);
    }

    size = (Cardinal)(strlen(buffer) + 1);
    if (toVal->addr != NULL) {
	if (toVal->size < size) {
	    toVal->size = size;
	    return (False);
	}
	strcpy((char *)toVal->addr, buffer);
    }
    else
	toVal->addr = (XPointer)buffer;
    toVal->size = sizeof(String);

    return (True);
}

#ifndef OLDXAW
Bool
_XawTextSourceNewLineAtEOF(Widget w)
{
    TextSrcObject src = (TextSrcObject)w;
    XawTextBlock text;

    text.firstPos = 0;
    if ((text.format = (unsigned long)src->textSrc.text_format) == XawFmt8Bit)
	text.ptr = SrcNL;
    else
	text.ptr = (char*)SrcWNL;
    text.length = 1;

    return (XawTextSourceSearch(w, XawTextSourceScan(w, 0, XawstAll,
						     XawsdRight, 1, True) - 1,
				XawsdRight, &text) != XawTextSearchError);
}

void
_XawSourceAddText(Widget source, Widget text)
{
    TextSrcObject src = (TextSrcObject)source;
    Bool found = False;
    Cardinal i;

    for (i = 0; i < src->textSrc.num_text; i++)
	if (src->textSrc.text[i] == text) {
	    found = True;
	    break;
	}

    if (!found) {
	src->textSrc.text = (WidgetList)
	    XtRealloc((char*)src->textSrc.text,
		      (Cardinal)(sizeof(Widget) * (src->textSrc.num_text + 1)));
	src->textSrc.text[src->textSrc.num_text++] = text;
    }
}

void
_XawSourceRemoveText(Widget source, Widget text, Bool destroy)
{
    TextSrcObject src = (TextSrcObject)source;
    Bool found = False;
    Cardinal i;

    if (src == NULL)
	return;

    for (i = 0; i < src->textSrc.num_text; i++)
	if (src->textSrc.text[i] == text) {
	    found = True;
	    break;
	}

    if (found) {
	if (--src->textSrc.num_text == 0) {
	    if (destroy) {
		XtDestroyWidget(source);
		return;
	    }
	    else {
		XtFree((char*)src->textSrc.text);
		src->textSrc.text = NULL;	/* for realloc "magic" */
	    }
	}
	else if (i < src->textSrc.num_text)
	    memmove(&src->textSrc.text[i], &src->textSrc.text[i + 1],
		    sizeof(Widget) * (src->textSrc.num_text - i));
    }
}
#endif /* OLDXAW */

/*
 * Function:
 *	XawTextSourceRead
 *
 * Parameters:
 *	w      - TextSrc Object
 *	pos    - position of the text to retrieve
 *	text   - text block that will contain returned text (return)
 *	length - maximum number of characters to read
 *
 * Description:
 *	This function reads the source.
 *
 * Returns:
 *	The number of characters read into the buffer
 */
XawTextPosition
XawTextSourceRead(Widget w, XawTextPosition pos, XawTextBlock *text,
		  int length)
{
  TextSrcObjectClass cclass = (TextSrcObjectClass)w->core.widget_class;

  return ((*cclass->textSrc_class.Read)(w, pos, text, length));
}

#ifndef OLDXAW
static void
TellSourceChanged(TextSrcObject src, XawTextPosition left,
		  XawTextPosition right, XawTextBlock *block, int lines)
{
    Cardinal i;

    for (i = 0; i < src->textSrc.num_text; i++)
	_XawTextSourceChanged(src->textSrc.text[i], left, right, block, lines);
}

/*
 * This function is required because there is no way to diferentiate
 * if the first erase was generated by a backward-kill-char and the
 * second by a forward-kill-char (or vice-versa) from XawTextSourceReplace.
 * It is only possible to diferentiate after the second character is
 * killed, but then, it is too late.
 */
void
_XawSourceSetUndoErase(TextSrcObject src, int value)
{
    if (src && src->textSrc.enable_undo)
	src->textSrc.undo->erase = value;
}

/*
 * To differentiate insert-char's separated by cursor movements.
 */
void
_XawSourceSetUndoMerge(TextSrcObject src, Bool state)
{
    if (src && src->textSrc.enable_undo)
	src->textSrc.undo->merge += state ? 1 : -1;
}
#endif /* OLDXAW */

/*
 * Public Functions
 */
/*
 * Function:
 *	XawTextSourceReplace
 *
 * Parameters:
 *	src	 - Text Source Object
 *	startPos - ends of text that will be removed
 *	endPos	 - ""
 *	text	 - new text to be inserted into buffer at startPos
 *
 * Description:
 *	Replaces a block of text with new text.
 *
 * Returns:
 *	XawEditError or XawEditDone.
 */
/*ARGSUSED*/
int
XawTextSourceReplace(Widget w, XawTextPosition left,
		      XawTextPosition right, XawTextBlock *block)
{
    TextSrcObjectClass cclass = (TextSrcObjectClass)w->core.widget_class;
#ifndef OLDXAW
    TextSrcObject src = (TextSrcObject)w;
    XawTextUndoBuffer *l_state, *r_state;
    XawTextUndoList *undo;
    Bool enable_undo;
    XawTextPosition start, end;
    int i, error, lines = 0;

    if (src->textSrc.edit_mode == XawtextRead)
	return (XawEditError);

    enable_undo = src->textSrc.enable_undo && src->textSrc.undo_state == False;
    if (enable_undo) {
	unsigned size, total;

	if (src->textSrc.undo->l_save) {
	    l_state = src->textSrc.undo->l_save;
	    src->textSrc.undo->l_save = NULL;
	}
	else
	    l_state = XtNew(XawTextUndoBuffer);
	l_state->refcount = 1;
	l_state->position = left;
	if (left < right) {
	    Widget ctx = NULL;

	    for (i = 0; i < src->textSrc.num_text; i++)
		if (XtIsSubclass(src->textSrc.text[i], textWidgetClass)) {
		    ctx = src->textSrc.text[i];
		    break;
		}
	    l_state->buffer = _XawTextGetText((TextWidget)ctx, left, right);
	    l_state->length = (unsigned)(right - left);
	}
	else {
	    l_state->length = 0;
	    l_state->buffer = NULL;
	}
	l_state->format = (unsigned long)src->textSrc.text_format;
	if (l_state->length == 1) {
	    if (l_state->format == XawFmtWide &&
		*(wchar_t*)l_state->buffer == *SrcWNL) {
		XtFree(l_state->buffer);
		l_state->buffer = (char*)SrcWNL;
	    }
	    else if (*l_state->buffer == '\n') {
		XtFree(l_state->buffer);
		l_state->buffer = SrcNL;
	    }
	}

	if (src->textSrc.undo->r_save) {
	    r_state = src->textSrc.undo->r_save;
	    src->textSrc.undo->r_save = NULL;
	}
	else
	    r_state = XtNew(XawTextUndoBuffer);
	r_state->refcount = 1;
	r_state->position = left;
	r_state->format = block->format;
	size = block->format == XawFmtWide ? sizeof(wchar_t) : sizeof(char);
	total = (size * (unsigned)block->length);
	r_state->length = (unsigned)block->length;
	r_state->buffer = NULL;
	if (total == size) {
	    if (r_state->format == XawFmtWide &&
		*(wchar_t*)block->ptr == *SrcWNL)
		r_state->buffer = (char*)SrcWNL;
	    else if (*block->ptr == '\n')
		r_state->buffer = SrcNL;
	}
	if (total && !r_state->buffer) {
	    r_state->buffer = XtMalloc(total);
	    memcpy(r_state->buffer, block->ptr, total);
	}

	if (src->textSrc.undo->u_save) {
	    undo = src->textSrc.undo->u_save;
	    src->textSrc.undo->u_save = NULL;
	}
	else
	    undo = XtNew(XawTextUndoList);
	undo->left = l_state;
	undo->right = r_state;
	undo->undo = src->textSrc.undo->list;
	undo->redo = NULL;
    }
    else {
	undo = NULL;
	l_state = r_state = NULL;
    }

#define	LARGE_VALUE	262144	/* 256 K */
    /* optimization, to avoid long delays recalculating the line number
     * when editing huge files
     */
    if (left > LARGE_VALUE) {
	start = XawTextSourceScan(w, left, XawstEOL, XawsdLeft, 2, False);
	for (i = 0; i < src->textSrc.num_text; i++) {
	    TextWidget tw = (TextWidget)src->textSrc.text[i];

	    if (left <= tw->text.lt.top &&
		left + block->length - (right - left) > tw->text.lt.top)
		_XawTextBuildLineTable(tw, start, False);
	}
    }
#undef LARGE_VALUE

    start = left;
    end = right;
    while (start < end) {
	start = XawTextSourceScan(w, start, XawstEOL, XawsdRight, 1, True);
	if (start <= end) {
	    --lines;
	    if (start == XawTextSourceScan(w, 0, XawstAll, XawsdRight, 1, True)) {
		lines += !_XawTextSourceNewLineAtEOF(w);
		break;
	    }
	}
    }
#else
    int error;
#endif /* OLDXAW */

    error = (*cclass->textSrc_class.Replace)(w, left, right, block);

#ifndef OLDXAW
    if (error != XawEditDone) {
	if (enable_undo) {
	    if (l_state->buffer) {
		if (l_state->buffer != SrcNL && l_state->buffer != (char*)SrcWNL)
		    XtFree(l_state->buffer);
		l_state->buffer = NULL;
	    }
	     src->textSrc.undo->l_save = l_state;
	     if (r_state->buffer) {
		if (r_state->buffer != SrcNL && r_state->buffer != (char*)SrcWNL)
		    XtFree(r_state->buffer);
		r_state->buffer = NULL;
	    }
	    src->textSrc.undo->r_save = r_state;

	    src->textSrc.undo->u_save = undo;
	}
    }
    else if (enable_undo) {
	XawTextUndoList *list = src->textSrc.undo->list;
	XawTextUndoBuffer *unl, *lnl;
	int erase = undo->right->length == 0 && undo->left->length == 1 && list
		    && list->right->length == 0;

	if (erase) {
	    erase = list->left->position - 1 == undo->left->position ? -1 :
		    list->left->position == undo->left->position ? 1 : 0;
	    if (src->textSrc.undo->erase && erase != src->textSrc.undo->erase)
		erase = 0;
	    else
		src->textSrc.undo->erase = erase;
	}

	if (erase) {
	    unl = l_state;
	    lnl = list->left;
	}
	else {
	    unl = r_state;
	    lnl = list ? list->right : NULL;
	}

	/* Try to merge the undo buffers */
	if (src->textSrc.undo->merge > 0 && ((erase ||
	     (list && ((list->left->length == 0 && undo->left->length == 0) ||
		       (list->left->length == list->right->length &&
			undo->left->length == 1)) &&
	      undo->right->length == 1 &&
	      list->right->position + list->right->length
	      == undo->right->position))
	    && src->textSrc.undo->pointer == list
	    && unl->format == list->right->format
	    && ((unl->format == XawFmt8Bit && unl->buffer[0] != XawLF) ||
		(unl->format == XawFmtWide &&
		 *(wchar_t*)(unl->buffer) != _Xaw_atowc(XawLF)))
	    && ((lnl->format == XawFmt8Bit && lnl->buffer[0] != XawLF) ||
		(lnl->format == XawFmtWide &&
		 *(wchar_t*)(lnl->buffer) != _Xaw_atowc(XawLF))))) {
	    unsigned size = lnl->format == XawFmtWide ?
		sizeof(wchar_t) : sizeof(char);

	    if (!erase) {
		list->right->buffer = XtRealloc(list->right->buffer,
						(list->right->length + 1) * size);
		memcpy(list->right->buffer + list->right->length * size,
		       undo->right->buffer, size);
		++list->right->length;
		XtFree(r_state->buffer);
	    }
	    else if (erase < 0) {
		--list->left->position;
		--list->right->position;
	    }

	    src->textSrc.undo->l_save = l_state;
	    src->textSrc.undo->r_save = r_state;
	    src->textSrc.undo->u_save = undo;

	    if (list->left->length) {
		list->left->buffer = XtRealloc(list->left->buffer,
					       (list->left->length + 1) * size);
		if (erase >= 0)
		    memcpy(list->left->buffer + list->left->length * size,
			   undo->left->buffer, size);
		else {
		    /* use memmove, since strings overlap */
		    memmove(list->left->buffer + size, list->left->buffer,
			    list->left->length * size);
		    memcpy(list->left->buffer, undo->left->buffer, size);
		}
		++list->left->length;
		if (l_state->buffer != SrcNL && l_state->buffer != (char*)SrcWNL)
		    XtFree(l_state->buffer);
	    }

	    if (src->textSrc.undo->num_list >= UNDO_DEPTH)
		UndoGC(src->textSrc.undo);
	}
	else {
	    src->textSrc.undo->undo = (XawTextUndoBuffer**)
		XtRealloc((char*)src->textSrc.undo->undo,
			  (Cardinal)((2 + src->textSrc.undo->num_undo)
				     * sizeof(XawTextUndoBuffer)));
	    src->textSrc.undo->undo[src->textSrc.undo->num_undo++] = l_state;
	    src->textSrc.undo->undo[src->textSrc.undo->num_undo++] = r_state;

	    if (src->textSrc.undo->list)
		src->textSrc.undo->list->redo = undo;
	    else
		src->textSrc.undo->head = undo;

	    src->textSrc.undo->merge = l_state->length <= 1 &&
				       r_state->length <= 1;

	    src->textSrc.undo->list = src->textSrc.undo->pointer =
		src->textSrc.undo->end_mark = undo;

	    if (++src->textSrc.undo->num_list >= UNDO_DEPTH)
		UndoGC(src->textSrc.undo);
	}
	src->textSrc.undo->dir = XawsdLeft;
	if (!src->textSrc.changed) {
	    src->textSrc.undo->l_no_change = src->textSrc.undo->list->right;
	    src->textSrc.undo->r_no_change = src->textSrc.undo->list->left;
	    src->textSrc.changed = True;
	}
    }
    else if (!src->textSrc.enable_undo)
	src->textSrc.changed = True;

    if (error == XawEditDone) {
	XawTextPropertyInfo info;
	XawTextAnchor *anchor;

	/* find anchor and index */
	/* XXX index (i) could be returned by XawTextSourceFindAnchor
	 * or similar function, to speed up */
	if ((anchor = XawTextSourceFindAnchor(w, left))) {
	    XawTextEntity *eprev, *entity, *enext;
	    XawTextPosition offset = 0, diff = block->length - (right - left);

	    for (i = 0; i < src->textSrc.num_anchors; i++)
		if (src->textSrc.anchors[i] == anchor)
		    break;
	    if (anchor->cache && anchor->position + anchor->cache->offset +
		anchor->cache->length <= left)
		eprev = entity = anchor->cache;
	    else
		eprev = entity = anchor->entities;
	    while (entity) {
		offset = anchor->position + entity->offset;

		if (offset > left)
		    break;
		if (offset + entity->length > left)
		    break;

		eprev = entity;
		entity = entity->next;
	    }

	    /* try to do the right thing here (and most likely correct), but
	     * other code needs to check what was done */

	    /* adjust entity length */
	    if (entity && offset <= left) {
		if (offset + entity->length < right)
		    entity->length = (Cardinal)(left - offset + block->length);
		else
		    entity->length = (Cardinal)(entity->length + diff);

		if (entity->length == 0) {
		    enext = entity->next;
		    eprev->next = enext;
		    anchor->cache = NULL;
		    XtFree((XtPointer)entity);
		    if (entity == anchor->entities) {
			if ((anchor->entities = enext) == NULL) {
			    eprev = NULL;
			    anchor = XawTextSourceRemoveAnchor(w, anchor);
			    entity = anchor ? anchor->entities : NULL;
			}
			else
			    eprev = entity = enext;
		    }
		    else
			entity = enext;
		}
		else {
		    eprev = entity;
		    entity = entity->next;
		}
	    }

	    while (anchor) {
		while (entity) {
		    offset = anchor->position + entity->offset + entity->length;

		    if (offset > right) {
			entity->length = (XawMin(entity->length, offset - right));
			goto exit_anchor_loop;
		    }

		    enext = entity->next;
		    if (eprev)
			eprev->next = enext;
		    XtFree((XtPointer)entity);
		    anchor->cache = NULL;
		    if (entity == anchor->entities) {
			eprev = NULL;
			if ((anchor->entities = enext) == NULL) {
			    if (i == 0)
				++i;
			    else if (i < --src->textSrc.num_anchors) {
				memmove(&src->textSrc.anchors[i],
					&src->textSrc.anchors[i + 1],
					(size_t)(src->textSrc.num_anchors - i) *
					sizeof(XawTextAnchor*));
				XtFree((XtPointer)anchor);
			    }
			    if (i >= src->textSrc.num_anchors) {
				anchor = NULL;
				entity = NULL;
				break;
			    }
			    anchor = src->textSrc.anchors[i];
			    entity = anchor->entities;
			    continue;
			}
		    }
		    entity = enext;
		}
		if (i + 1 < src->textSrc.num_anchors) {
		    anchor = src->textSrc.anchors[++i];
		    entity = anchor->entities;
		    eprev = NULL;
		}
		else {
		    anchor = NULL;
		    break;
		}
		eprev = NULL;
	    }

exit_anchor_loop:
	    if (anchor) {
		XawTextAnchor *aprev;

		if (anchor->position >= XawMax(right, left + block->length))
		    anchor->position += diff;
		else if (anchor->position > left &&
			 (aprev = XawTextSourcePrevAnchor(w, anchor))) {
		    XawTextPosition tmp = anchor->position - aprev->position;

		    if (diff) {
			while (entity) {
			    entity->offset += diff;
			    entity = entity->next;
			}
		    }
		    entity = anchor->entities;
		    while (entity) {
			entity->offset += tmp;
			entity = entity->next;
		    }
		    if ((entity = aprev->entities) == NULL)
			aprev->entities = anchor->entities;
		    else {
			while (entity->next)
			    entity = entity->next;
			entity->next = anchor->entities;
		    }
		    anchor->entities = NULL;
		    (void)XawTextSourceRemoveAnchor(w, anchor);
		    --i;
		}
		else if (diff) {
		    while (entity) {
			entity->offset += diff;
			entity = entity->next;
		    }
		}
	    }

	    if (diff) {
		/*   The first anchor is never removed, and should
		 * have position 0.
		 *   i should be -1 if attempted to removed the first
		 * anchor, what can be caused when removing a chunk
		 * of text of the first entity.
		 * */
		if (++i == 0) {
		    anchor = src->textSrc.anchors[0];
		    eprev = entity = anchor->entities;
		    while (entity) {
			enext = entity->next;
			if (entity->offset + entity->length <= -diff)
			    XtFree((XtPointer)entity);
			else
			    break;
			entity = enext;
		    }
		    if (eprev != entity) {
			anchor->cache = NULL;
			if ((anchor->entities = entity) != NULL) {
			    if ((entity->offset += diff) < 0) {
				entity->length = (Cardinal)(entity->length + entity->offset);
				entity->offset = 0;
			    }
			}
		    }
		    ++i;
		}
		for (; i < src->textSrc.num_anchors; i++)
		    src->textSrc.anchors[i]->position += diff;
	    }
	}

	start = left;
	end = start + block->length;
	while (start < end) {
	    start = XawTextSourceScan(w, start, XawstEOL, XawsdRight, 1, True);
	    if (start <= end) {
		++lines;
		if (start == XawTextSourceScan(w, 0, XawstAll, XawsdRight, 1, True)) {
		    lines -= !_XawTextSourceNewLineAtEOF(w);
		    break;
		}
	    }
	}

	info.left = left;
	info.right = right;
	info.block = block;
	XtCallCallbacks(w, XtNpropertyCallback, &info);

	TellSourceChanged(src, left, right, block, lines);
	/* Call callbacks, we have changed the buffer */
	XtCallCallbacks(w, XtNcallback,
			(XtPointer)((long)src->textSrc.changed));
    }

#endif /* OLDXAW */
    return (error);
}

#ifndef OLDXAW
Bool
_XawTextSrcUndo(TextSrcObject src, XawTextPosition *insert_pos)
{
    static wchar_t wnull = 0;
    XawTextBlock block;
    XawTextUndoList *list, *nlist;
    XawTextUndoBuffer *l_state, *r_state;
    Boolean changed = src->textSrc.changed;

    if (!src->textSrc.enable_undo || !src->textSrc.undo->num_undo)
	return (False);

    list = src->textSrc.undo->pointer;

    if (src->textSrc.undo->dir == XawsdLeft) {
	l_state = list->right;
	r_state = list->left;
    }
    else {
	l_state = list->left;
	r_state = list->right;
    }

    if (src->textSrc.undo->l_no_change == l_state
	&& src->textSrc.undo->r_no_change == r_state)
	src->textSrc.changed = False;
    else
	src->textSrc.changed = True;

    block.firstPos = 0;
    block.length = (int)r_state->length;
    block.ptr = r_state->buffer ? r_state->buffer : (char*)&wnull;
    block.format = r_state->format;

    src->textSrc.undo_state = True;
    if (XawTextSourceReplace((Widget)src, l_state->position, l_state->position
			     + l_state->length, &block) != XawEditDone) {
	src->textSrc.undo_state = False;
	src->textSrc.changed = changed;
	return (False);
    }
    src->textSrc.undo_state = False;

    ++l_state->refcount;
    ++r_state->refcount;
    nlist = XtNew(XawTextUndoList);
    nlist->left = l_state;
    nlist->right = r_state;
    nlist->undo = src->textSrc.undo->list;
    nlist->redo = NULL;

    if (list == src->textSrc.undo->list)
	src->textSrc.undo->end_mark = nlist;

    if (src->textSrc.undo->dir == XawsdLeft) {
	if (list->undo == NULL)
	    src->textSrc.undo->dir = XawsdRight;
	else
	    list = list->undo;
    }
    else {
	if (list->redo == NULL || list->redo == src->textSrc.undo->end_mark)
	    src->textSrc.undo->dir = XawsdLeft;
	else
	    list = list->redo;
    }
    *insert_pos = r_state->position + r_state->length;
    src->textSrc.undo->pointer = list;
    src->textSrc.undo->list->redo = nlist;
    src->textSrc.undo->list = nlist;
    src->textSrc.undo->merge = src->textSrc.undo->erase = 0;

    if (++src->textSrc.undo->num_list >= UNDO_DEPTH)
	UndoGC(src->textSrc.undo);

    return (True);
}

Bool
_XawTextSrcToggleUndo(TextSrcObject src)
{
    if (!src->textSrc.enable_undo || !src->textSrc.undo->num_undo)
	return (False);

    if (src->textSrc.undo->pointer != src->textSrc.undo->list) {
	if (src->textSrc.undo->dir == XawsdLeft) {
	    if (src->textSrc.undo->pointer->redo
		&& (src->textSrc.undo->pointer->redo
		    != src->textSrc.undo->end_mark)) {
		src->textSrc.undo->pointer = src->textSrc.undo->pointer->redo;
		src->textSrc.undo->dir = XawsdRight;
	    }
	}
	else {
	    if (src->textSrc.undo->pointer->undo
		&& (src->textSrc.undo->pointer != src->textSrc.undo->head)) {
		src->textSrc.undo->pointer = src->textSrc.undo->pointer->undo;
		src->textSrc.undo->dir = XawsdLeft;
	    }
	}
    }

    return (True);
}

static void
FreeUndoBuffer(XawTextUndo *undo)
{
    unsigned i;
    XawTextUndoList *head, *del;

    for (i = 0; i < undo->num_undo; i++) {
	if (undo->undo[i]->buffer && undo->undo[i]->buffer != SrcNL &&
	    undo->undo[i]->buffer != (char*)SrcWNL)
	    XtFree(undo->undo[i]->buffer);
	XtFree((char*)undo->undo[i]);
    }
    XtFree((char*)undo->undo);
    head = undo->head;

    del = head;
    while (head) {
	head = head->redo;
	XtFree((char*)del);
	del = head;
    }

    if (undo->l_save) {
	XtFree((char*)undo->l_save);
	undo->l_save = NULL;
    }
    if (undo->r_save) {
	XtFree((char*)undo->r_save);
	undo->r_save = NULL;
    }
    if (undo->u_save) {
	XtFree((char*)undo->u_save);
	undo->u_save = NULL;
    }

    undo->list = undo->pointer = undo->head = undo->end_mark = NULL;
    undo->l_no_change = undo->r_no_change = NULL;
    undo->undo = NULL;
    undo->dir = XawsdLeft;
    undo->num_undo = undo->num_list = (unsigned)(undo->erase = undo->merge = 0);
}

static void
UndoGC(XawTextUndo *undo)
{
    unsigned i;
    XawTextUndoList *head = undo->head, *redo = head->redo;

    if (head == undo->pointer || head == undo->end_mark
	|| undo->l_no_change == NULL
	|| head->left == undo->l_no_change || head->right == undo->l_no_change)
      return;

    undo->head = redo;
    redo->undo = NULL;

    --head->left->refcount;
    if (--head->right->refcount == 0) {
	for (i = 0; i < undo->num_undo; i+= 2)
	    if (head->left == undo->undo[i] || head->left == undo->undo[i+1]) {
		if (head->left == undo->undo[i+1]) {
		    XawTextUndoBuffer *tmp = redo->left;

		    redo->left = redo->right;
		    redo->right = tmp;
		}
		if (head->left->buffer && head->left->buffer != SrcNL &&
		    head->left->buffer != (char*)SrcWNL)
		    XtFree(head->left->buffer);
		XtFree((char*)head->left);
		if (head->right->buffer && head->right->buffer != SrcNL &&
		    head->right->buffer != (char*)SrcWNL)
		    XtFree(head->right->buffer);
		XtFree((char*)head->right);

		undo->num_undo -= 2;
		memmove(&undo->undo[i], &undo->undo[i + 2],
			(undo->num_undo - i) * sizeof(XawTextUndoBuffer*));
		break;
	    }
    }
    XtFree((char*)head);
    --undo->num_list;
}
#endif /* OLDXAW */

/*
 * Function:
 *	XawTextSourceScan
 *
 * Parameters:
 *	w	 - TextSrc Object
 *	position - position to start scanning
 *	type	 - type of thing to scan for
 *	dir	 - direction to scan
 *	count	 - which occurrence if this thing to search for
 *	include  - whether or not to include the character found in
 *		   the position that is returned.
 *
 * Description:
 *	Scans the text source for the number and type of item specified.
 *
 * Returns:
 *	The position of the text
 */
XawTextPosition
XawTextSourceScan(Widget w, XawTextPosition position,
#if NeedWidePrototypes
		  int type, int dir, int count, int include
#else
		  XawTextScanType type, XawTextScanDirection dir,
		  int count, Boolean include
#endif
)
{
    TextSrcObjectClass cclass = (TextSrcObjectClass)w->core.widget_class;

    return ((*cclass->textSrc_class.Scan)
	    (w, position, type, dir, count, include));
}

/*
 * Function:
 *	XawTextSourceSearch
 *
 * Parameters:
 *	w	 - TextSource Object
 *	position - position to start scanning
 *	dir	 - direction to scan
 *	text	 - the text block to search for.
 *
 * Returns:
 *	The position of the text we are searching for or XawTextSearchError.
 *
 * Description:
 *	Searches the text source for the text block passed
 */
XawTextPosition
XawTextSourceSearch(Widget w, XawTextPosition position,
#if NeedWidePrototypes
		    int dir,
#else
		    XawTextScanDirection dir,
#endif
		    XawTextBlock *text)
{
    TextSrcObjectClass cclass = (TextSrcObjectClass)w->core.widget_class;

    return ((*cclass->textSrc_class.Search)(w, position, dir, text));
}

/*
 * Function:
 *	XawTextSourceConvertSelection
 *
 * Parameters:
 *	w	  - TextSrc object
 *	selection - current selection atom
 *	target	  - current target atom
 *	type	  - type to convert the selection to
 *	value	  - return value that has been converted
 *	length	  - ""
 *	format	  - format of the returned value
 *
 * Returns:
 *	True if the selection has been converted
 */
Boolean
XawTextSourceConvertSelection(Widget w, Atom *selection, Atom *target,
			      Atom *type, XtPointer *value,
			      unsigned long *length, int *format)
{
    TextSrcObjectClass cclass = (TextSrcObjectClass)w->core.widget_class;

    return((*cclass->textSrc_class.ConvertSelection)
	   (w, selection, target, type, value, length, format));
}

/*
 * Function:
 *	XawTextSourceSetSelection
 *
 * Parameters:
 *	w	  - TextSrc object
 *	left	  - bounds of the selection
 *	right	  - ""
 *	selection - selection atom
 *
 * Description:
 *	Allows special setting of the selection.
 */
void
XawTextSourceSetSelection(Widget w, XawTextPosition left,
			  XawTextPosition right, Atom selection)
{
    TextSrcObjectClass cclass = (TextSrcObjectClass)w->core.widget_class;

    (*cclass->textSrc_class.SetSelection)(w, left, right, selection);
}

/*
 * External Functions for Multi Text
 */
/*
 * TextFormat():
 *	returns the format of text: FMT8BIT or FMTWIDE
 */
XrmQuark
_XawTextFormat(TextWidget tw)
{
    return (((TextSrcObject)(tw->text.source))->textSrc.text_format);
}

/* _XawTextWCToMB():
 *	Convert the wchar string to external encoding
 *	The caller is responsible for freeing both the source and ret string
 *
 *	wstr	   - source wchar string
 *	len_in_out - length of string.
 *		     As In, length of source wchar string, measured in wchar
 *		     As Out, length of returned string
 */
char *
_XawTextWCToMB(Display *d, wchar_t *wstr, int *len_in_out)
{
    XTextProperty textprop;

    if (XwcTextListToTextProperty(d, (wchar_t**)&wstr, 1,
				XTextStyle, &textprop) < Success) {
	XtWarningMsg("convertError", "textSource", "XawError",
		     "Non-character code(s) in buffer.", NULL, NULL);
	*len_in_out = 0;
	return (NULL);
    }
    *len_in_out = (int)textprop.nitems;

    return ((char *)textprop.value);
}

/* _XawTextMBToWC():
 *	Convert the string to internal processing codeset WC.
 *   The caller is responsible for freeing both the source and ret string.
 *
 *	str	   - source string
 *	len_in_out - length of string
 *		     As In, it is length of source string
 *		     As Out, it is length of returned string, measured in wchar
 */
wchar_t *
_XawTextMBToWC(Display *d, char *str, int *len_in_out)
{
    XTextProperty textprop;
    char *buf;
    wchar_t **wlist, *wstr;
    int count;

    if (*len_in_out == 0)
	return (NULL);

    buf = XtMalloc((Cardinal)(*len_in_out + 1));

    strncpy(buf, str, (size_t)*len_in_out);
    *(buf + *len_in_out) = '\0';
    if (XmbTextListToTextProperty(d, &buf, 1, XTextStyle, &textprop) != Success) {
	XtWarningMsg("convertError", "textSource", "XawError",
		     "No Memory, or Locale not supported.", NULL, NULL);
	XtFree(buf);
	*len_in_out = 0;
	return (NULL);
    }

    XtFree(buf);
    if (XwcTextPropertyToTextList(d, &textprop,
				  (wchar_t***)&wlist, &count) != Success) {
	XtWarningMsg("convertError", "multiSourceCreate", "XawError",
		     "Non-character code(s) in source.", NULL, NULL);
	*len_in_out = 0;
	return (NULL);
    }
    wstr = wlist[0];
    *len_in_out = (int)wcslen(wstr);
    XtFree((XtPointer)wlist);

    return (wstr);
}

#ifndef OLDXAW
static int
qcmp_anchors(_Xconst void *left, _Xconst void *right)
{
    return (int)((*(XawTextAnchor**)left)->position -
		 (*(XawTextAnchor**)right)->position);
}

XawTextAnchor *
XawTextSourceAddAnchor(Widget w, XawTextPosition position)
{
    TextSrcObject src = (TextSrcObject)w;
    XawTextAnchor *anchor, *panchor;

    if ((panchor = XawTextSourceFindAnchor(w, position)) != NULL) {
	XawTextEntity *pentity, *entity;

	if (position - panchor->position < ANCHORS_DIST)
	    return (panchor);

	if (panchor->cache && panchor->position + panchor->cache->offset +
	    panchor->cache->length < position)
	    pentity = entity = panchor->cache;
	else
	    pentity = entity = panchor->entities;

	while (entity && panchor->position + entity->offset +
	       entity->length < position) {
	    pentity = entity;
	    entity = entity->next;
	}
	if (entity) {
	    XawTextPosition diff;

	    if (panchor->position + entity->offset < position)
		position = panchor->position + entity->offset;

	    if (position == panchor->position)
		return (panchor);

	    anchor = XtNew(XawTextAnchor);
	    diff = position - panchor->position;

	    panchor->cache = NULL;
	    anchor->entities = entity;
	    if (pentity != entity)
		pentity->next = NULL;
	    else
		panchor->entities = NULL;
	    while (entity) {
		entity->offset -= diff;
		entity = entity->next;
	    }
	}
	else {
	    anchor = XtNew(XawTextAnchor);
	    anchor->entities = NULL;
	}
    }
    else {
	anchor = XtNew(XawTextAnchor);
	anchor->entities = NULL;
    }

    anchor->position = position;
    anchor->cache = NULL;

    src->textSrc.anchors = (XawTextAnchor**)
	XtRealloc((XtPointer)src->textSrc.anchors,
		  (Cardinal)(sizeof(XawTextAnchor*) *
			     (size_t)(src->textSrc.num_anchors + 1)));
    src->textSrc.anchors[src->textSrc.num_anchors++] = anchor;
    qsort((void*)src->textSrc.anchors,
	  (size_t)src->textSrc.num_anchors,
	  sizeof(XawTextAnchor*), qcmp_anchors);

    return (anchor);
}

XawTextAnchor *
XawTextSourceFindAnchor(Widget w, XawTextPosition position)
{
    TextSrcObject src = (TextSrcObject)w;
    int i = 0, left, right, nmemb = src->textSrc.num_anchors;
    XawTextAnchor *anchor, **anchors = src->textSrc.anchors;

    left = 0;
    right = nmemb - 1;
    while (left <= right) {
	anchor = anchors[i = (left + right) >> 1];
	if (anchor->position == position)
	    return (anchor);
	else if (position < anchor->position)
	    right = i - 1;
	else
	    left = i + 1;
    }

    if (nmemb)
	return (right < 0 ? anchors[0] : anchors[right]);

    return (NULL);
}

Bool
XawTextSourceAnchorAndEntity(Widget w, XawTextPosition position,
			     XawTextAnchor **anchor_return,
			     XawTextEntity **entity_return)
{
    XawTextAnchor *anchor = XawTextSourceFindAnchor(w, position);
    XawTextEntity *pentity, *entity;
    XawTextPosition offset;
    Bool next_anchor = True, retval = False;

    if (anchor->cache && anchor->position + anchor->cache->offset +
	anchor->cache->length <= position)
	pentity = entity = anchor->cache;
    else
	pentity = entity = anchor->entities;
    while (entity) {
	offset = anchor->position + entity->offset;

	if (offset > position) {
	    retval = next_anchor = False;
	    break;
	}
	if (offset + entity->length > position) {
	    retval = True;
	    next_anchor = False;
	    break;
	}
	pentity = entity;
	entity = entity->next;
    }

    if (next_anchor) {
	*anchor_return = anchor = XawTextSourceNextAnchor(w, anchor);
	*entity_return = anchor ? anchor->entities : NULL;
    }
    else {
	*anchor_return = anchor;
	*entity_return = retval ? entity : pentity;
    }

    if (*anchor_return)
	(*anchor_return)->cache = *entity_return;

    return (retval);
}

XawTextAnchor *
XawTextSourceNextAnchor(Widget w, XawTextAnchor *anchor)
{
    int i;
    TextSrcObject src = (TextSrcObject)w;

    for (i = 0; i < src->textSrc.num_anchors - 1; i++)
	if (src->textSrc.anchors[i] == anchor)
	    return (src->textSrc.anchors[i + 1]);

    return (NULL);
}

XawTextAnchor *
XawTextSourcePrevAnchor(Widget w, XawTextAnchor *anchor)
{
    int i;
    TextSrcObject src = (TextSrcObject)w;

    for (i = src->textSrc.num_anchors - 1; i > 0; i--)
	if (src->textSrc.anchors[i] == anchor)
	    return (src->textSrc.anchors[i - 1]);

    return (NULL);
}

XawTextAnchor *
XawTextSourceRemoveAnchor(Widget w, XawTextAnchor *anchor)
{
    int i;
    TextSrcObject src = (TextSrcObject)w;

    for (i = 0; i < src->textSrc.num_anchors; i++)
	if (src->textSrc.anchors[i] == anchor)
	    break;

    if (i == 0)
	return (src->textSrc.num_anchors > 1 ? src->textSrc.anchors[1] : NULL);

    if (i < src->textSrc.num_anchors) {
	XtFree((XtPointer)anchor);
	if (i < --src->textSrc.num_anchors) {
	    memmove(&src->textSrc.anchors[i],
		    &src->textSrc.anchors[i + 1],
		    (size_t)(src->textSrc.num_anchors - i) *
		    sizeof(XawTextAnchor*));

	    return (src->textSrc.anchors[i]);
	}
    }

    return (NULL);
}

XawTextEntity *
XawTextSourceAddEntity(Widget w, int type, int flags, XtPointer data,
		       XawTextPosition position, Cardinal length,
		       XrmQuark property)
{
    XawTextAnchor *next, *anchor = _XawTextSourceFindAnchor(w, position);
    XawTextEntity *entity, *eprev;

    /* There is no support for zero length entities for now */
    if (length == 0)
	return (NULL);

    if (anchor->cache && anchor->position + anchor->cache->offset +
	anchor->cache->length <= position)
	eprev = entity = anchor->cache;
    else
	eprev = entity = anchor->entities;

    while (entity && anchor->position + entity->offset + entity->length <=
	   position) {
	eprev = entity;
	entity = entity->next;
    }
    if (entity && anchor->position + entity->offset < position + length) {
	fprintf(stderr, "Cannot (yet) add more than one entity to same region.\n");
	return (NULL);
    }

    next = XawTextSourceFindAnchor(w, position + length);
    if (next && next != anchor) {
	if ((entity = next->entities) != NULL) {
	    if (next->position + entity->offset < position + length) {
		fprintf(stderr, "Cannot (yet) add more than one entity to same region.\n");
		return (NULL);
	    }
	}
	if (position + length > next->position) {
	    XawTextPosition diff = position + length - next->position;

	    next->position += diff;
	    entity = next->entities;
	    while (entity) {
		entity->offset -= diff;
		entity = entity->next;
	    }
	    entity = anchor->entities;
	    while (entity && entity->offset < 0)
		entity = entity->next;
	    if (entity && entity->offset < 0) {
		if (eprev)
		    eprev->next = next->entities;
		else
		    anchor->entities = next->entities;
		if ((next->entities = entity->next) == NULL)
		    (void)XawTextSourceRemoveAnchor(w, next);
		entity->next = NULL;

		return (XawTextSourceAddEntity(w, type, flags, data, position,
					       length, property));
	    }
	}
    }

    /* Automatically join sequential entities if possible */
    if (eprev &&
	anchor->position + eprev->offset + eprev->length == position &&
	eprev->property == property && eprev->type == type &&
	eprev->flags == flags && eprev->data == data) {
	eprev->length += length;
	return (eprev);
    }

    entity = XtNew(XawTextEntity);
    entity->type = (short)type;
    entity->flags = (short)flags;
    entity->data = data;
    entity->offset = position - anchor->position;
    entity->length = length;
    entity->property = property;

    if (eprev == NULL) {
	anchor->entities = entity;
	entity->next = NULL;
	anchor->cache = NULL;
    }
    else if (eprev->offset > entity->offset) {
	anchor->cache = NULL;
	anchor->entities = entity;
	entity->next = eprev;
    }
    else {
	anchor->cache = eprev;
	entity->next = eprev->next;
	eprev->next = entity;
    }

    return (entity);
}

void
XawTextSourceClearEntities(Widget w, XawTextPosition left, XawTextPosition right)
{
    XawTextAnchor *anchor = XawTextSourceFindAnchor(w, left);
    XawTextEntity *entity, *eprev, *enext;
    XawTextPosition offset;
    int length;

    while (anchor && anchor->entities == NULL)
	anchor = XawTextSourceRemoveAnchor(w, anchor);

    if (anchor == NULL || left >= right)
	return;

    if (anchor->cache && anchor->position + anchor->cache->offset +
	anchor->cache->length < left)
	eprev = entity = anchor->cache;
    else
	eprev = entity = anchor->entities;

    /* find first entity before left position */
    while (anchor->position + entity->offset + entity->length < left) {
	eprev = entity;
	if ((entity = entity->next) == NULL) {
	    if ((anchor = XawTextSourceNextAnchor(w, anchor)) == NULL)
		return;
	    if ((eprev = entity = anchor->entities) == NULL) {
		fprintf(stderr, "Bad anchor found!\n");
		return;
	    }
	}
    }

    offset = anchor->position + entity->offset;
    if (offset <= left) {
	length = (XawMin(entity->length, left - offset));

	if (length <= 0) {
	    enext = entity->next;
	    eprev->next = enext;
	    XtFree((XtPointer)entity);
	    anchor->cache = NULL;
	    if (entity == anchor->entities) {
		eprev = NULL;
		if ((anchor->entities = enext) == NULL) {
		    if ((anchor = XawTextSourceRemoveAnchor(w, anchor)) == NULL)
			return;
		    entity = anchor->entities;
		}
		else
		    entity = enext;
	    }
	    else
		entity = enext;
	}
	else {
	    entity->length = (Cardinal)length;
	    eprev = entity;
	    entity = entity->next;
	}
    }

    /* clean everything until right position is reached */
    while (anchor) {
	while (entity) {
	    offset = anchor->position + entity->offset + entity->length;

	    if (offset > right) {
		anchor->cache = NULL;
		entity->offset = XawMax(entity->offset, right - anchor->position);
		entity->length = (XawMin(entity->length, offset - right));
		return;
	    }

	    enext = entity->next;
	    if (eprev)
		eprev->next = enext;
	    XtFree((XtPointer)entity);
	    if (entity == anchor->entities) {
		eprev = anchor->cache = NULL;
		if ((anchor->entities = enext) == NULL) {
		    if ((anchor = XawTextSourceRemoveAnchor(w, anchor)) == NULL)
			return;
		    entity = anchor->entities;
		    continue;
		}
	    }
	    entity = enext;
	}
	if (anchor)
	    anchor->cache = NULL;
	if ((anchor = XawTextSourceNextAnchor(w, anchor)) != NULL)
	    entity = anchor->entities;
	eprev = NULL;
    }
}

/* checks the anchors up to position, and create an appropriate anchor
 * at position, if required.
 */
XawTextAnchor *
_XawTextSourceFindAnchor(Widget w, XawTextPosition position)
{
    XawTextAnchor *anchor;

    anchor = XawTextSourceFindAnchor(w, position);

    position -= position % ANCHORS_DIST;

    if (position - anchor->position >= ANCHORS_DIST)
	return (XawTextSourceAddAnchor(w, position));

    return (anchor);
}
#endif
