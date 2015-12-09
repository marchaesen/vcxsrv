/*
 * Copyright 1991 by OMRON Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name OMRON not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  OMRON makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * OMRON DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OMRON BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTUOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 *      Authors: Chris Peterson MIT X Consortium
 *               Li Yuhong      OMRON Corporation
 *               Frank Sheeran  OMRON Corporation
 *
 * Much code taken from X11R3 String and Disk Sources.
 */

/*

Copyright 1991, 1994, 1998  The Open Group

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
#include <ctype.h>
#include <errno.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xfuncs.h>
#include <X11/Xos.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/MultiSrcP.h>
#include <X11/Xaw/XawImP.h>
#include "XawI18n.h"
#include "Private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAGIC_VALUE	((XawTextPosition)-1)
#define streq(a, b)	(strcmp((a), (b)) == 0)


/*
 * Class Methods
 */
static XawTextPosition ReadText(Widget, XawTextPosition, XawTextBlock*, int);
static int  ReplaceText(Widget, XawTextPosition, XawTextPosition,
			XawTextBlock*);
static XawTextPosition Scan(Widget, XawTextPosition, XawTextScanType,
			    XawTextScanDirection, int, Bool);
static XawTextPosition  Search(Widget, XawTextPosition, XawTextScanDirection,
			       XawTextBlock*);
static void XawMultiSrcClassInitialize(void);
static void XawMultiSrcDestroy(Widget);
static void XawMultiSrcInitialize(Widget, Widget, ArgList, Cardinal*);
static Boolean XawMultiSrcSetValues(Widget, Widget, Widget,
				    ArgList, Cardinal*);
static void XawMultiSrcGetValuesHook(Widget, ArgList, Cardinal*);

/*
 * Prototypes
 */
static MultiPiece *AllocNewPiece(MultiSrcObject, MultiPiece*);
static void BreakPiece(MultiSrcObject, MultiPiece*);
static Boolean CvtMultiTypeToString(Display*, XrmValuePtr, Cardinal*,
				    XrmValuePtr, XrmValuePtr, XtPointer*);
static void CvtStringToMultiType(XrmValuePtr, Cardinal*,
				 XrmValuePtr, XrmValuePtr);
static MultiPiece *FindPiece(MultiSrcObject, XawTextPosition,
			     XawTextPosition*);
static void FreeAllPieces(MultiSrcObject);
static FILE *InitStringOrFile(MultiSrcObject, Bool);
static void LoadPieces(MultiSrcObject, FILE*, char*);
static void RemovePiece(MultiSrcObject, MultiPiece*);
static void RemoveOldStringOrFile(MultiSrcObject, Bool);
static String StorePiecesInString(MultiSrcObject);
static Bool WriteToFile(String, String);
static void GetDefaultPieceSize(Widget, int, XrmValue*);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(MultiSrcRec, multi_src.field)
static XtResource resources[] = {
  {
    XtNstring,
    XtCString,
    XtRString,
    sizeof(XtPointer),
    offset(string),
    XtRPointer,
    NULL
  },
  {
    XtNtype,
    XtCType,
    XtRMultiType,
    sizeof(XawAsciiType),
    offset(type),
    XtRImmediate,
    (XtPointer)XawAsciiString
  },
  {
    XtNdataCompression,
    XtCDataCompression,
    XtRBoolean,
    sizeof(Boolean),
    offset(data_compression),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNpieceSize,
    XtCPieceSize,
    XtRInt,
    sizeof(XawTextPosition),
    offset(piece_size),
    XtRCallProc,
    (XtPointer)GetDefaultPieceSize
  },
#ifdef OLDXAW
  {
    XtNcallback,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(callback),
    XtRCallback,
    (XtPointer)NULL
  },
#endif
  {
    XtNuseStringInPlace,
    XtCUseStringInPlace,
    XtRBoolean,
    sizeof(Boolean),
    offset(use_string_in_place),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNlength,
    XtCLength,
    XtRInt,
    sizeof(int),
    offset(multi_length),
    XtRImmediate,
    (XtPointer)MAGIC_VALUE
  },
};
#undef offset

#define superclass		(&textSrcClassRec)
MultiSrcClassRec multiSrcClassRec = {
  /* object */
  {
    (WidgetClass)superclass,		/* superclass */
    "MultiSrc",				/* class_name */
    sizeof(MultiSrcRec),		/* widget_size */
    XawMultiSrcClassInitialize,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawMultiSrcInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    NULL,				/* obj1 */
    NULL,				/* obj2 */
    0,					/* obj3 */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    False,				/* obj4 */
    False,				/* obj5 */
    False,				/* obj6 */
    False,				/* obj7 */
    XawMultiSrcDestroy,			/* destroy */
    NULL,				/* obj8 */
    NULL,				/* obj9 */
    XawMultiSrcSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    NULL,				/* obj10 */
    XawMultiSrcGetValuesHook,		/* get_values_hook */
    NULL,				/* obj11 */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* obj12 */
    NULL,				/* obj13 */
    NULL,				/* obj14 */
    NULL,				/* extension */
  },
  /* text_src */
  {
    ReadText,				/* Read */
    ReplaceText,			/* Replace */
    Scan,				/* Scan */
    Search,				/* Search */
    XtInheritSetSelection,		/* SetSelection */
    XtInheritConvertSelection,		/* ConvertSelection */
  },
  /* multi_src */
  {
    NULL,				/* extension */
  },
};

WidgetClass multiSrcObjectClass = (WidgetClass)&multiSrcClassRec;

static XrmQuark Qstring, Qfile;

/*
 * Implementation
 */
static void
XawMultiSrcClassInitialize(void)
{
    XawInitializeWidgetSet();
    Qstring = XrmPermStringToQuark(XtEstring);
    Qfile = XrmPermStringToQuark(XtEfile);
    XtAddConverter(XtRString, XtRMultiType, CvtStringToMultiType, NULL, 0);
    XtSetTypeConverter(XtRMultiType, XtRString, CvtMultiTypeToString, NULL, 0,
		       XtCacheNone, NULL);
}

/*
 * Function:
 *	XawMultiSrcInitialize
 *
 * Parameters:
 *	request  - widget requested by the argument list
 *	cnew	 - the new widget with both resource and non resource values
 *	args	 - (unused)
 *	num_args - (unused)
 *
 * Description:
 *	Initializes the multi src object
 */
/*ARGSUSED*/
static void
XawMultiSrcInitialize(Widget request, Widget cnew,
		      ArgList args, Cardinal *num_args)
{
    MultiSrcObject src = (MultiSrcObject)cnew;
    FILE *file;

    /*
     * Set correct flags (override resources) depending upon widget class
     */
#ifdef OLDXAW
    src->multi_src.changes = False;
#else
    src->text_src.changed = False;
#endif
    src->multi_src.allocated_string = False;

    if (src->multi_src.use_string_in_place && src->multi_src.string == NULL)
	src->multi_src.use_string_in_place = False;

    file = InitStringOrFile(src, src->multi_src.type == XawAsciiFile);
    LoadPieces(src, file, NULL);

    if (file != NULL)
	fclose(file);
    src->text_src.text_format = XawFmtWide;
}

/*
 * Function:
 *	ReadText
 *
 * Parameters:
 *	w      - MultiSource object
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
static XawTextPosition
ReadText(Widget w, XawTextPosition pos, XawTextBlock *text, int length)
{
    MultiSrcObject src = (MultiSrcObject)w;
    XawTextPosition count, start;
    MultiPiece *piece = FindPiece(src, pos, &start);

    text->format = XawFmtWide;
    text->firstPos = pos;
    text->ptr = (char *)(piece->text + (pos - start));
    count = piece->used - (pos - start);
    text->length = Max(0, (length > count) ? count : length);

    return (pos + text->length);
}

/*
 * Function:
 *	ReplaceText
 *
 * Parameters:
 *	w	 - MultiSource object
 *	startPos - ends of text that will be removed
 *	endPos	 - ""
 *	text	 - new text to be inserted into buffer at startPos
 *
 * Description:
 *	Replaces a block of text with new text.
 *
 * Returns:
 *	XawEditDone on success, XawEditError otherwise
 */
/*ARGSUSED*/
static int
ReplaceText(Widget w, XawTextPosition startPos, XawTextPosition endPos,
	    XawTextBlock *u_text_p)
{
    MultiSrcObject src = (MultiSrcObject)w;
    MultiPiece *start_piece, *end_piece, *temp_piece;
    XawTextPosition start_first, end_first;
    int length, firstPos;
    wchar_t *wptr;
    Bool local_artificial_block = False;
    XawTextBlock text;

    /* STEP 1: The user handed me a text block called `u_text' that may be
     * in either FMTWIDE or FMT8BIT (ie MB.)  Later code needs the block
     * `text' to hold FMTWIDE.	So, this copies `u_text' to `text', and if
     * `u_text' was MB, I knock it up to WIDE
     */
    if (u_text_p->length == 0)	/* if so, the block contents never ref'd */
	text.length = 0;

    else if (u_text_p->format == XawFmtWide) {
	local_artificial_block = False; /* don't have to free it ourselves */
	text.firstPos = u_text_p->firstPos;
	text.length =	u_text_p->length;
	text.ptr =	u_text_p->ptr;
    }
    else {
	/*
	 * WARNING! u_text->firstPos and length are in units of CHAR,
	 * not CHARACTERS!
	 */
	local_artificial_block = True;	/* have to free it ourselves */
	text.firstPos = 0;
	text.length = u_text_p->length; /* _XawTextMBToWC converts this
					 * to wchar len
					 */

	text.ptr = (char*)_XawTextMBToWC(XtDisplay(XtParent(w)),
					 &u_text_p->ptr[u_text_p->firstPos],
					 &text.length);

	/* I assert the following assignment is not needed - since Step 4
	   depends on length, it has no need of a terminating NULL.  I think
	   the ASCII-version has the same needless NULL. */
	/*((wchar_t*)text.ptr)[ text.length ] = NULL;*/
    }

    /* STEP 2: some initialization... */
    if (src->text_src.edit_mode == XawtextRead)
	return (XawEditError);

    start_piece = FindPiece(src, startPos, &start_first);
    end_piece = FindPiece(src, endPos, &end_first);

    /* STEP 3: remove the empty pieces... */
    if (start_piece != end_piece) {
	temp_piece = start_piece->next;

	/* If empty and not the only piece then remove it */
	if (((start_piece->used = startPos - start_first) == 0)
	    &&	!(start_piece->next == NULL && start_piece->prev == NULL))
	    RemovePiece(src, start_piece);

	while (temp_piece != end_piece) {
	    temp_piece = temp_piece->next;
	    RemovePiece(src, temp_piece->prev);
	}
	end_piece->used -= endPos - end_first;
	if (end_piece->used != 0)
	    memmove(end_piece->text, end_piece->text + endPos - end_first,
		    end_piece->used * sizeof(wchar_t));
    }
    else {		    /* We are fully in one piece */
	if ((start_piece->used -= endPos - startPos) == 0) {
	    if (!(start_piece->next == NULL && start_piece->prev == NULL))
		RemovePiece(src, start_piece);
	}
	else {
	    memmove(start_piece->text + (startPos - start_first),
		    start_piece->text + (endPos - start_first),
		    (start_piece->used - (startPos - start_first)) *
		    sizeof(wchar_t));
	    if (src->multi_src.use_string_in_place &&
		((src->multi_src.length - (endPos - startPos))
		< src->multi_src.piece_size - 1))
		start_piece->text[src->multi_src.length - (endPos - startPos)] =
		  (wchar_t)0;
	}
    }

    src->multi_src.length += text.length -(endPos - startPos);

    /* STEP 4: insert the new stuff */
    if ( text.length != 0) {
        start_piece = FindPiece(src, startPos, &start_first);
        length = text.length;
        firstPos = text.firstPos;

	while (length > 0) {
	    wchar_t *ptr;
	    int fill;

	    if (src->multi_src.use_string_in_place) {
		if (start_piece->used == src->multi_src.piece_size - 1)  {

		    /*
		     * The string is used in place, then the string
		     * is not allowed to grow
		     */
		    start_piece->used = src->multi_src.length =
			src->multi_src.piece_size - 1;

		    start_piece->text[src->multi_src.length] = (wchar_t)0;
		    return (XawEditError);
		}
	    }

	    if (start_piece->used == src->multi_src.piece_size) {
		BreakPiece(src, start_piece);
		start_piece = FindPiece(src, startPos, &start_first);
	    }

	    fill = Min((int)(src->multi_src.piece_size - start_piece->used), length);

	    ptr = start_piece->text + (startPos - start_first);
	    memmove(ptr + fill, ptr, (start_piece->used -
		    (startPos - start_first)) * sizeof(wchar_t));
	    wptr =(wchar_t *)text.ptr;
	    (void)wcsncpy(ptr, wptr + firstPos, fill);

	    startPos += fill;
	    firstPos += fill;
	    start_piece->used += fill;
	    length -= fill;
	}
    }

    if (local_artificial_block == True)
	/* In other words, text is not the u_text that the user handed me but
	   one I made myself.  I only care, because I need to free the string */
	XtFree(text.ptr);

    if (src->multi_src.use_string_in_place)
	start_piece->text[start_piece->used] = (wchar_t)0;

#ifdef OLDXAW
    src->multi_src.changes = True;
    XtCallCallbacks(w, XtNcallback, NULL);
#endif

    return (XawEditDone);
}

/*
 * Function:
 *	Scan
 *
 * Parameters:
 *	w	 - MultiSource widget
 *	position - position to start scanning
 *	type	 - type of thing to scan for
 *	dir	 - direction to scan
 *	count	 - which occurance if this thing to search for
 *		   include - whether or not to include the character found in
 *		   the position that is returned
 *
 * Description:
 *	Scans the text source for the number and type of item specified.
 *
 * Returns:
 *	The position of the item found
 *
 * Note:
 *	  While there are only 'n' characters in the file there are n+1
 *	 possible cursor positions (one before the first character and
 *	one after the last character
 */
static XawTextPosition
Scan(Widget w, register XawTextPosition position, XawTextScanType type,
     XawTextScanDirection dir, int count, Bool include)
{
    MultiSrcObject src = (MultiSrcObject)w;
    register char inc;
    MultiPiece *piece;
    XawTextPosition first, first_eol_position = position;
    register wchar_t *ptr;
    int cnt = count;

    if (type == XawstAll) {
	if (dir == XawsdRight)
	    return (src->multi_src.length);
	return (0);
    }

    /* STEP 1: basic sanity checks */
    if (position > src->multi_src.length)
	position = src->multi_src.length;

    if (dir == XawsdRight) {
	if (position == src->multi_src.length)
	    return (src->multi_src.length);
	inc = 1;
    }
    else {
	if (position == 0)
	    return (0);
	inc = -1;
	position--;
    }

    piece = FindPiece(src, position, &first);

    if (piece->used == 0)
	return (0);

    ptr = (position - first) + piece->text;

    switch (type) {
	case XawstEOL:
	case XawstParagraph:
	case XawstWhiteSpace:
	case XawstAlphaNumeric:
	    for (; cnt > 0 ; cnt--) {
		Bool non_space = False, first_eol = True;

		/*CONSTCOND*/
		while (True) {
		    register wchar_t c;

		    if (ptr < piece->text) {
			piece = piece->prev;
			if (piece == NULL)	/* Begining of text */
			    return (0);
			ptr = piece->text + piece->used - 1;
			c = *ptr;
		    }
		    else if (ptr >= piece->text + piece->used) {
			piece = piece->next;
			if (piece == NULL)	/* End of text */
			    return (src->multi_src.length);
			ptr = piece->text;
		    }

		    c = *ptr;
		    ptr += inc;
		    position += inc;

		    if (type == XawstAlphaNumeric) {
			if (!iswalnum(c)) {
			    if (non_space)
				break;
			}
			else
			    non_space = True;
		    }
		    else if (type == XawstWhiteSpace) {
			if (iswspace(c)) {
			    if (non_space)
			      break;
			}
			else
			    non_space = True;
		    }
		    else if (type == XawstEOL) {
			if (c == _Xaw_atowc(XawLF))
			    break;
		    }
		    else {	/* XawstParagraph */
			if (first_eol) {
			    if (c == _Xaw_atowc(XawLF)) {
				first_eol_position = position;
				first_eol = False;
			    }
			}
			else
			    if (c == _Xaw_atowc(XawLF))
				break;
			else if (!iswspace(c))
			    first_eol = True;
		    }
		}
	    }
	    if (!include) {
		if (type == XawstParagraph)
		    position = first_eol_position;
		if (count)
		    position -= inc;
	    }
	    break;
	case XawstPositions:
	    position += count * inc;
	    break;
	default:
	    break;
    }

    if (dir == XawsdLeft)
	position++;

    if (position >= src->multi_src.length)
	return (src->multi_src.length);
    if (position < 0)
	return (0);

    return (position);
}

/*
 * Function:
 *	Search
 *
 * Parameters:
 *	w	 - MultiSource objecy
 *	position - position to start scanning
 *	dir	 - direction to scan
 *	text	 - text block to search for
 *
 * Description:
 *	Searchs the text source for the text block passed.
 *
 * Returns:
 *	The position of the item found
 */
static XawTextPosition
Search(Widget w, register XawTextPosition position, XawTextScanDirection dir,
       XawTextBlock *text)
{
    MultiSrcObject src = (MultiSrcObject)w;
    register int count = 0;
    wchar_t *ptr;
    wchar_t *wtarget;
    int wtarget_len;
    Display *d = XtDisplay(XtParent(w));
    MultiPiece *piece;
    wchar_t *buf;
    XawTextPosition first;
    register char inc;
    int cnt;

    /* STEP 1: First, a brief sanity check */
    if (dir == XawsdRight)
	inc = 1;
    else  {
	inc = -1;
	if (position == 0)
	    return (XawTextSearchError);
	position--;
    }

    /* STEP 2: Ensure I have a local wide string.. */

    /* Since this widget stores 32bit chars, I check here to see if
       I'm being passed a string claiming to be 8bit chars (ie, MB text.)
       If that is the case, naturally I convert to 32bit format */

    /*if the block was FMT8BIT, length will convert to REAL wchar count bellow */
    wtarget_len = text->length;

    if (text->format == XawFmtWide)
	wtarget = &(((wchar_t*)text->ptr) [text->firstPos]);
    else {
	/* The following converts wtarget_len from byte len to wchar count */
	   wtarget = _XawTextMBToWC(d, &text->ptr[text->firstPos], &wtarget_len);
    }

    /* OK, I can now assert that wtarget holds wide characters, wtarget_len
       holds an accurate count of those characters, and that firstPos has been
       effectively factored out of the following computations */

    /* STEP 3: SEARCH! */
    buf = (wchar_t *)XtMalloc(sizeof(wchar_t) * wtarget_len);
    (void)wcsncpy(buf, wtarget, wtarget_len);
    piece = FindPiece(src, position, &first);
    ptr = (position - first) + piece->text;

    /*CONSTCOND*/
    while (True) {
	if (*ptr == (dir == XawsdRight ? *(buf + count)
		     : *(buf + wtarget_len - count - 1))) {
	    if (count == text->length - 1)
		break;
	    else
		count++;
	}
	else {
	    if (count != 0) {
		position -=inc * count;
		ptr -= inc * count;
	    }
	    count = 0;
	}

	ptr += inc;
	position += inc;

	while (ptr < piece->text) {
	    cnt = piece->text - ptr;

	    piece = piece->prev;
	    if (piece == NULL) {	/* Begining of text */
		XtFree((char *)buf);
		return (XawTextSearchError);
	    }
	    ptr = piece->text + piece->used - cnt;
	}

	while (ptr >= piece->text + piece->used) {
	    cnt = ptr - (piece->text + piece->used);

	    piece = piece->next;
	    if (piece == NULL) {	/* End of text */
		XtFree((char *)buf);
		return (XawTextSearchError);
	    }
	    ptr = piece->text + cnt;
	}
    }

    XtFree((char *)buf);
    if (dir == XawsdLeft)
	return(position);

    return(position - (wtarget_len - 1));
}

/*
 * Function:
 *	XawMultiSrcSetValues
 *
 * Parameters:
 *	current  - current state of the widget
 *	request  - what was requested
 *	cnew	 - what the widget will become
 *	args	 - representation of resources that have changed
 *	num_args - number of changed resources
 *
 * Description:
 *	Sets the values for the MultiSource.
 *
 * Returns:
 *	True if redisplay is needed
 */
static Boolean
XawMultiSrcSetValues(Widget current, Widget request, Widget cnew,
		     ArgList args, Cardinal *num_args)
{
    MultiSrcObject src = (MultiSrcObject)cnew;
    MultiSrcObject old_src = (MultiSrcObject)current;
    XtAppContext app_con = XtWidgetToApplicationContext(cnew);
    Bool total_reset = False, string_set = False;
    FILE *file;
    unsigned int i;

    if (old_src->multi_src.use_string_in_place
	!= src->multi_src.use_string_in_place) {
	XtAppWarning(app_con,
		     "MultiSrc: The XtNuseStringInPlace resources "
		     "may not be changed.");
	src->multi_src.use_string_in_place =
	    old_src->multi_src.use_string_in_place;
    }

    for (i = 0; i < *num_args ; i++)
	if (streq(args[i].name, XtNstring)) {
	    string_set = True;
	    break;
	}

    if (string_set || old_src->multi_src.type != src->multi_src.type) {
	RemoveOldStringOrFile(old_src, string_set);
	src->multi_src.allocated_string = old_src->multi_src.allocated_string;
	file = InitStringOrFile(src, string_set);

        LoadPieces(src, file, NULL);
	if (file != NULL)
	    fclose(file);
#ifndef OLDXAW
	for (i = 0; i < src->text_src.num_text; i++)
	    /* Tell text widget what happened */
	    XawTextSetSource(src->text_src.text[i], cnew, 0);
#else
	XawTextSetSource(XtParent(cnew), cnew, 0);
#endif
	total_reset = True;
    }

    if (old_src->multi_src.multi_length != src->multi_src.multi_length)
	src->multi_src.piece_size = src->multi_src.multi_length + 1;

    if ( !total_reset && old_src->multi_src.piece_size
	 != src->multi_src.piece_size) {
	String mb_string = StorePiecesInString(old_src);

	if (mb_string != 0) {
	    FreeAllPieces(old_src);
	    LoadPieces(src, NULL, mb_string);
	    XtFree(mb_string);
	}
	else {
	    /* If the buffer holds bad chars, don't touch it... */
	    XtAppWarningMsg(app_con,
			    "convertError", "multiSource", "XawError",
			     XtName(XtParent((Widget)old_src)), NULL, NULL);
	    XtAppWarningMsg(app_con,
			    "convertError", "multiSource", "XawError",
			    "Non-character code(s) in buffer.", NULL, NULL);
	}
    }

    return (False);
}

static void
XawMultiSrcGetValuesHook(Widget w, ArgList args, Cardinal *num_args)
{
    MultiSrcObject src = (MultiSrcObject)w;
    unsigned int i;

    if (src->multi_src.type == XawAsciiString) {
	for (i = 0; i < *num_args ; i++) {
	    if (streq(args[i].name, XtNstring)) {
		if (src->multi_src.use_string_in_place)
		    *((char **)args[i].value) = (char *)
			src->multi_src.first_piece->text;
		else if (_XawMultiSave(w))	/* If save sucessful */
		    *((char **)args[i].value) = (char *)src->multi_src.string;
		break;
	    }
	}
    }
}

static void
XawMultiSrcDestroy(Widget w)
{
    RemoveOldStringOrFile((MultiSrcObject) w, True);
}

/*
 * Public routines
 */
/*
 * Function:
 *	XawMultiSourceFreeString
 *
 * Parameters:
 *	w - MultiSrc widget
 *
 * Description:
 *	  Frees the string returned by a get values call
 *                   on the string when the source is of type string.
 *
 * Note:
 * The public interface is XawAsciiSourceFreeString!
 */
void
_XawMultiSourceFreeString(Widget w)
{
    MultiSrcObject src = (MultiSrcObject)w;

    if (src->multi_src.allocated_string) {
	XtFree((char *)src->multi_src.string);
	src->multi_src.allocated_string = False;
	src->multi_src.string = NULL;
    }
}

/*
 * Function:
 *	_XawMultiSave
 *
 * Parameters:
 *	w - multiSrc Widget
 *
 * Description:
 *	Saves all the pieces into a file or string as required.
 *
 * Returns:
 *	True if the save was successful
 *
 * Note:
 * The public interface is XawAsciiSave(w)!
 */
Bool
_XawMultiSave(Widget w)
{
    MultiSrcObject src = (MultiSrcObject)w;
    XtAppContext app_con = XtWidgetToApplicationContext(w);
    char *mb_string;

    /*
     * If using the string in place then there is no need to play games
     * to get the internal info into a readable string
     */
    if (src->multi_src.use_string_in_place)
	return (True);

    if (src->multi_src.type == XawAsciiFile) {
#ifdef OLDXAW
	 if (!src->multi_src.changes)
#else
	if (!src->text_src.changed)		/* No changes to save */
#endif
	    return (True);

	mb_string = StorePiecesInString(src);

	if (mb_string != 0) {
	    if (WriteToFile(mb_string, (String)src->multi_src.string) == False) {
		XtFree(mb_string);
		return (False);
	    }
	    XtFree(mb_string);
#ifndef OLDXAW
	    src->text_src.changed = False;
#else
	    src->multi_src.changes = False;
#endif
	    return (True);
	}
	else {
	    /* If the buffer holds bad chars, don't touch it... */
	    XtAppWarningMsg(app_con,
			    "convertError", "multiSource", "XawError",
			    "Due to illegal characters, file not saved.",
			    NULL, NULL);
	    return (False);
	}
    }
    else  {
    /* THIS FUNCTIONALITY IS UNDOCUMENTED, probably UNNEEDED?  The manual
	   says this routine's only function is to save files to
	   disk.  -Sheeran */
	mb_string = StorePiecesInString(src);

	if (mb_string == 0) {
	    /* If the buffer holds bad chars, don't touch it... */
	    XtAppWarningMsg(app_con,
			    "convertError", "multiSource", "XawError",
			    XtName(XtParent((Widget)src)), NULL, NULL);
	    return (False);
	}

	/* assert: mb_string holds good characters so the buffer is fine */
	if (src->multi_src.allocated_string == True)
	    XtFree((char *)src->multi_src.string);
	else
	    src->multi_src.allocated_string = True;

        src->multi_src.string = mb_string;
    }
#ifdef OLDXAW
    src->multi_src.changes = False;
#else
    src->text_src.changed = False;
#endif

    return (True);
}

/*
 * Function:
 *	XawMultiSaveAsFile
 *
 * Parameters:
 *	w - MultiSrc widget
 *	name - name of the file to save this file into
 *
 * Description:
 *	Save the current buffer as a file.
 *
 * Returns:
 *	True if the save was sucessful
 *
 * Note:
 * The public interface is XawAsciiSaveAsFile!
 */
Bool
_XawMultiSaveAsFile(Widget w, _Xconst char* name)
{
    MultiSrcObject src = (MultiSrcObject)w;
    String mb_string;
    Bool ret;

    mb_string = StorePiecesInString(src);

    if (mb_string != 0) {
	ret = WriteToFile(mb_string, (char *)name);
	XtFree(mb_string);

	return (ret);
    }

    /* otherwise there was a conversion error.	So print widget name too */
    XtAppWarningMsg(XtWidgetToApplicationContext(w),
		    "convertError", "multiSource", "XawError",
		    XtName(XtParent(w)), NULL, NULL);

    return (False);
}

/*
 * Private Functions
 */
static void
RemoveOldStringOrFile(MultiSrcObject src, Bool checkString)
{
    FreeAllPieces(src);

    if (checkString && src->multi_src.allocated_string) {
	XtFree((char *)src->multi_src.string);
	src->multi_src.allocated_string = False;
	src->multi_src.string = NULL;
    }
}

/*
 * Function:
 *	WriteToFile
 *
 * Parameters:
 *	string - string to write
 *	name   - name of the file
 *
 * Description:
 *	Write the string specified to the begining of the file  specified.
 *
 * Returns:
 *	Returns True if sucessful, False otherwise
 */
static Bool
WriteToFile(String string, String name)
{
    int fd;
    Bool result = True;

    if ((fd = creat(name, 0666)) == -1)
	return (False);

    if (write(fd, string, strlen(string)) == -1)
        result = False;

    if (close(fd) == -1)
	return (False);

    return (result);
}


/*
 * Function:
 *	StorePiecesInString
 *
 * Parameters:
 *	src - the multiSrc object to gather data from
 *
 * Description:
 *	Store the pieces in memory into a char string.
 *
 * Returns:
 *	mb_string:	Caller must free
 *	(or)
 *	NULL:		conversion error
 */
static String
StorePiecesInString(MultiSrcObject src)
{
    wchar_t *wc_string;
    char *mb_string;
    int char_count = src->multi_src.length;
    XawTextPosition first;
    MultiPiece *piece;

    /* I believe the char_count + 1 and the NULL termination are unneeded! FS */
    wc_string = (wchar_t*)XtMalloc((char_count + 1) * sizeof(wchar_t));

    for (first = 0, piece = src->multi_src.first_piece ; piece != NULL;
	 first += piece->used, piece = piece->next)
	(void)wcsncpy(wc_string + first, piece->text, piece->used);

    wc_string[char_count] = 0;

    /* This will refill all pieces to capacity */
    if (src->multi_src.data_compression) {
	FreeAllPieces(src);
	LoadPieces(src, NULL, (char *)wc_string);
    }

    /* Lastly, convert it to a MB format and send it back */
    mb_string = _XawTextWCToMB(XtDisplayOfObject((Widget)src),
			       wc_string, &char_count);

    /* NOTE THAT mb_string MAY BE ZERO IF THE CONVERSION FAILED */
    XtFree((char*)wc_string);

    return (mb_string);
}

/*
 * Function:
 *	InitStringOrFile
 *
 * Parameters:
 *	src - MultiSource
 *
 * Description:
 *	Initializes the string or file.
 */
static FILE *
InitStringOrFile(MultiSrcObject src, Bool newString)
{
    mode_t open_mode = 0;
    const char *fdopen_mode = NULL;
    int fd;
    FILE *file;
    Display *d = XtDisplayOfObject((Widget)src);

    if (src->multi_src.type == XawAsciiString) {
	if (src->multi_src.string == NULL)
	    src->multi_src.length = 0;

	else if (!src->multi_src.use_string_in_place) {
	    int length;
	    String temp = XtNewString((char *)src->multi_src.string);

	    if (src->multi_src.allocated_string)
		XtFree((char *)src->multi_src.string);
	    src->multi_src.allocated_string = True;
	    src->multi_src.string = temp;

	    length = strlen((char *)src->multi_src.string);

	    /* Wasteful, throwing away the WC string, but need side effect! */
	    (void)_XawTextMBToWC(d, (char *)src->multi_src.string, &length);
	    src->multi_src.length = (XawTextPosition)length;
	}
	else {
	    src->multi_src.length = strlen((char *)src->multi_src.string);
	    /* In case the length resource is incorrectly set */
	    if (src->multi_src.length > src->multi_src.multi_length)
		src->multi_src.multi_length = src->multi_src.length;

	    if (src->multi_src.multi_length == MAGIC_VALUE)
		src->multi_src.piece_size = src->multi_src.length;
	    else
		src->multi_src.piece_size = src->multi_src.multi_length + 1;
	}

	return (NULL);
    }

    /*
     * type is XawAsciiFile
     */
    src->multi_src.is_tempfile = False;

    switch (src->text_src.edit_mode) {
	case XawtextRead:
	    if (src->multi_src.string == NULL)
		XtErrorMsg("NoFile", "multiSourceCreate", "XawError",
			   "Creating a read only disk widget and no file specified.",
			   NULL, 0);
	    open_mode = O_RDONLY;
	    fdopen_mode = "r";
	    break;
	case XawtextAppend:
	case XawtextEdit:
	    if (src->multi_src.string == NULL) {
		src->multi_src.string = "*multi-src*";
		src->multi_src.is_tempfile = True;
	    }
	    else {
/* O_NOFOLLOW is a BSD & Linux extension */
#ifdef O_NOFOLLOW
		open_mode = O_RDWR | O_NOFOLLOW;
#else
		open_mode = O_RDWR; /* unsafe; subject to race conditions */
#endif
		fdopen_mode = "r+";
	    }
	    break;
	default:
	    XtErrorMsg("badMode", "multiSourceCreate", "XawError",
		       "Bad editMode for multi source; must be "
		       "Read, Append or Edit.", NULL, NULL);
    }

    /* If is_tempfile, allocate a private copy of the text
     * Unlikely to be changed, just to set allocated_string */
    if (newString || src->multi_src.is_tempfile) {
	String temp = XtNewString((char *)src->multi_src.string);

	if (src->multi_src.allocated_string)
	    XtFree((char *)src->multi_src.string);
	src->multi_src.string = temp;
	src->multi_src.allocated_string = True;
    }

    if (!src->multi_src.is_tempfile) {
	if ((fd = open((char *)src->multi_src.string, open_mode, 0666)) != -1) {
	    if ((file = fdopen(fd, fdopen_mode)) != NULL) {
		(void)fseek(file, 0, SEEK_END);
		src->multi_src.length = (XawTextPosition)ftell(file);
		return(file);
	    }
	    else
		close(fd);
	}
	{
	    String params[2];
	    Cardinal num_params = 2;

	    params[0] = (String)src->multi_src.string;
	    params[1] = strerror(errno);
	    XtAppWarningMsg(XtWidgetToApplicationContext((Widget)src),
			    "openError", "multiSourceCreate", "XawWarning",
			    "Cannot open file %s; %s", params, &num_params);
	}
    }
    src->multi_src.length = 0;
    return (NULL);
}

/* LoadPieces:  This routine takes either the MB contents of open file
   `file' or the MB contents of string or the MB contents of
   src->multi_src.string and places them in Pieces in WC format.

   CAUTION: You must have src->multi_src.length set to file length bytes
   when src->multi_src.type == XawAsciiFile.  src->multi_src.length must be
   the length of the parameter string if string is non-NULL
*/
static void
LoadPieces(MultiSrcObject src, FILE *file, char *string)
{
    Display *d = XtDisplayOfObject((Widget)src);
    wchar_t* local_str, *ptr;
    MultiPiece* piece = NULL;
    XawTextPosition left;
    int bytes = sizeof(wchar_t);
    char* temp_mb_holder = NULL;

    /*
     * This is tricky - the _XawTextMBtoWC converter uses its 3rd arg
     * in as MB length, out as WC length.  We want local_length to be
     * WC count.
     */
    int local_length = src->multi_src.length;

    if (string != NULL) {
	/*
	 * ASSERT: IF our caller passed a non-null string, THEN
	 * src->multi_src.length is currently string's * byte count,
	 * AND string is in a MB format
	*/
	local_str = _XawTextMBToWC(d, (char *)string, &local_length);
	src->multi_src.length = (XawTextPosition) local_length;
    }
    else if (src->multi_src.type != XawAsciiFile) {
	/*
	 * here, we are not changing the contents, just reloading,
	 * so don't change len...
	 */
	local_length = src->multi_src.string ?
	    strlen((char *)src->multi_src.string) : 0;
	local_str = _XawTextMBToWC(d, (char *)src->multi_src.string,
				   &local_length);
    }
    else {
	if (src->multi_src.length != 0) {
	    temp_mb_holder =
		XtMalloc((src->multi_src.length + 1) * sizeof(unsigned char));
	    fseek(file, 0, SEEK_SET);
	    src->multi_src.length = fread(temp_mb_holder,
					  sizeof(unsigned char),
					  (size_t)src->multi_src.length, file);
	    if (src->multi_src.length <= 0)
		XtAppErrorMsg(XtWidgetToApplicationContext ((Widget) src),
			      "readError", "multiSource", "XawError",
			      "fread returned error.", NULL, NULL);
	    local_length = src->multi_src.length;
	    local_str = _XawTextMBToWC(d, temp_mb_holder, &local_length);
	    src->multi_src.length = local_length;

	    if (local_str == 0) {
		String params[2];
		Cardinal num_params;
		static char err_text[] =
		    "<<< FILE CONTENTS NOT REPRESENTABLE IN THIS LOCALE >>>";

		params[0] = XtName(XtParent((Widget)src));
		params[1] = src->multi_src.string;
		num_params = 2;

		XtAppWarningMsg(XtWidgetToApplicationContext((Widget)src),
				"readLocaleError", "multiSource", "XawError",
				"%s: The file `%s' contains characters "
				"not representable in this locale.",
				params, &num_params);
		src->multi_src.length = sizeof err_text;
		local_length = src->multi_src.length;
		local_str = _XawTextMBToWC(d, err_text, &local_length);
		src->multi_src.length = local_length;
	    }
	}
	else
	    /* ASSERT that since following while loop looks at local_length
	       this isn't needed.	Sheeran, Omron KK, 1993/07/15
	       temp_mb_holder[src->multi_src.length] = '\0'; */
	    local_str = (wchar_t*)temp_mb_holder;
    }

    if (src->multi_src.use_string_in_place) {
	piece = AllocNewPiece(src, piece);
	piece->used = Min(src->multi_src.length, src->multi_src.piece_size);
	piece->text = (wchar_t*)src->multi_src.string;
	return;
    }

    ptr = local_str;
    left = local_length;

    do {
	piece = AllocNewPiece(src, piece);

	piece->text = (wchar_t*)XtMalloc((unsigned)(src->multi_src.piece_size
						    * bytes));
	piece->used = Min(left, src->multi_src.piece_size);
	if (piece->used != 0)
	(void)wcsncpy(piece->text, ptr, piece->used);

	left -= piece->used;
	ptr += piece->used;
    } while (left > 0);

    if (temp_mb_holder)
	XtFree((char*)temp_mb_holder);
}

/*
 * Function:
 *	AllocNewPiece
 *
 * Parameters:
 *	src  - MultiSrc Widget
 *	prev - the piece just before this one, or NULL
 *
 * Description:
 *	Allocates a new piece of memory.
 *
 * Returns:
 *	The allocated piece
 */
static MultiPiece *
AllocNewPiece(MultiSrcObject src, MultiPiece *prev)
{
    MultiPiece *piece = XtNew(MultiPiece);

    if (prev == NULL) {
	src->multi_src.first_piece = piece;
	piece->next = NULL;
    }
    else {
	if (prev->next != NULL)
	    (prev->next)->prev = piece;
	piece->next = prev->next;
	prev->next = piece;
    }

    piece->prev = prev;

    return (piece);
}

/*
 * Function:
 *	FreeAllPieces
 *
 * Parameters:
 *	src - MultiSrc Widget
 *
 * Description:
 *	Frees all the pieces
 */
static void
FreeAllPieces(MultiSrcObject src)
{
    MultiPiece *next, *first = src->multi_src.first_piece;

#ifdef DEBUG
    if (first->prev != NULL)
	printf("Xaw MultiSrc Object: possible memory leak in FreeAllPieces().\n");
#endif

    for (; first != NULL ; first = next) {
	next = first->next;
	RemovePiece(src, first);
    }
}

/*
 * Function:
 *	RemovePiece
 *
 * Parameters:
 *	piece - piece to remove
 *
 * Description:
 *	Removes a piece from the list.
 */
static void
RemovePiece(MultiSrcObject src, MultiPiece *piece)
{
    if (piece->prev == NULL)
	src->multi_src.first_piece = piece->next;
    else
	piece->prev->next = piece->next;

    if (piece->next != NULL)
	piece->next->prev = piece->prev;

    if (!src->multi_src.use_string_in_place)
	XtFree((char *)piece->text);

    XtFree((char *)piece);
}

/*
 * Function:
 *	FindPiece
 *
 * Parameters:
 *	src - MultiSrc Widget
 *	position - position that we are searching for
 *	first - position of the first character in this piece (return)
 *
 * Description:
 *	Finds the piece containing the position indicated.
 *
 * Returns:
 *	Piece that contains this position
 */
static MultiPiece *
FindPiece(MultiSrcObject src, XawTextPosition position, XawTextPosition *first)
{
    MultiPiece *old_piece, *piece;
    XawTextPosition temp;

    for (old_piece = NULL, piece = src->multi_src.first_piece, temp = 0;
         piece; old_piece = piece, piece = piece->next)
	if ((temp += piece->used) > position) {
	    *first = temp - piece->used;
	    return (piece);
	}

    *first = temp - (old_piece ? old_piece->used : 0);

    return (old_piece);	  /* if we run off the end the return the last piece */
}

/*
 * Function:
 *	BreakPiece
 *
 * Parameters:
 *	src - MultiSrc Widget
 *	piece - piece to break
 *
 * Description:
 *	Breaks a full piece into two new pieces.
 */
#define HALF_PIECE (src->multi_src.piece_size >> 1)
static void
BreakPiece(MultiSrcObject src, MultiPiece *piece)
{
    MultiPiece *cnew = AllocNewPiece(src, piece);

    cnew->text = (wchar_t *)
	XtMalloc(src->multi_src.piece_size * sizeof(wchar_t));
    (void)wcsncpy(cnew->text, piece->text + HALF_PIECE,
		  src->multi_src.piece_size - HALF_PIECE);
    piece->used = HALF_PIECE;
    cnew->used = src->multi_src.piece_size - HALF_PIECE;
}

/*ARGSUSED*/
static void
CvtStringToMultiType(XrmValuePtr args, Cardinal *num_args,
		     XrmValuePtr fromVal, XrmValuePtr toVal)
{
    static XawAsciiType type = XawAsciiString;
    XrmQuark q;
    char name[7];

    XmuNCopyISOLatin1Lowered(name, (char *)fromVal->addr, sizeof(name));
    q = XrmStringToQuark(name);

    if (q == Qstring)
	type = XawAsciiString;
    if (q == Qfile)
	type = XawAsciiFile;
    else {
	toVal->size = 0;
	toVal->addr = NULL;
	XtStringConversionWarning((char *)fromVal->addr, XtRAsciiType);
    }

    toVal->size = sizeof(XawAsciiType);
    toVal->addr = (XPointer)&type;
}

/*ARGSUSED*/
static Boolean
CvtMultiTypeToString(Display *dpy, XrmValuePtr args, Cardinal *num_args,
		     XrmValuePtr fromVal, XrmValuePtr toVal,
		     XtPointer *data)
{
    static String buffer;
    Cardinal size;

    switch (*(XawAsciiType *)fromVal->addr) {
	case XawAsciiFile:
	    buffer = XtEfile;
	    break;
	case XawAsciiString:
	    buffer = XtEstring;
	    break;
	default:
	    XawTypeToStringWarning(dpy, XtRAsciiType);
	    toVal->addr = NULL;
	    toVal->size = 0;
	    return (False);
    }

    size = strlen(buffer) + 1;
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

/*ARGSUSED*/
static void
GetDefaultPieceSize(Widget w, int offset, XrmValue *value)
{
    static XPointer pagesize;

    if (pagesize == 0) {
	pagesize = (XPointer)((long)_XawGetPageSize());
	if (pagesize < (XPointer)BUFSIZ)
	    pagesize = (XPointer)BUFSIZ;
    }

    value->addr = (XPointer)&pagesize;
}
