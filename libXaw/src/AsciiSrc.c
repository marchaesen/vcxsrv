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
 * AsciiSrc.c - AsciiSrc object. (For use with the text widget).
 *
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
#include <X11/Xos.h>
#include <X11/Xfuncs.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/AsciiSrcP.h>
#include <X11/Xaw/MultiSrcP.h>
#ifndef OLDXAW
#include <X11/Xaw/TextSinkP.h>
#include <X11/Xaw/AsciiSinkP.h>
#endif
#include "Private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if (defined(ASCII_STRING) || defined(ASCII_DISK))
#include <X11/Xaw/AsciiText.h>		/* for Widget Classes */
#endif

#define MAGIC_VALUE	((XawTextPosition)-1)
#define streq(a, b)	(strcmp((a), (b)) == 0)

/*
 * Class Methods
 */
static void XawAsciiSrcClassInitialize(void);
static void XawAsciiSrcDestroy(Widget);
static void XawAsciiSrcGetValuesHook(Widget, ArgList, Cardinal*);
static void XawAsciiSrcInitialize(Widget, Widget, ArgList, Cardinal*);
static Boolean XawAsciiSrcSetValues(Widget, Widget, Widget,
				    ArgList, Cardinal*);
static XawTextPosition ReadText(Widget, XawTextPosition, XawTextBlock*, int);
static int ReplaceText(Widget, XawTextPosition, XawTextPosition,
		       XawTextBlock*);
static XawTextPosition Scan(Widget, XawTextPosition, XawTextScanType,
			    XawTextScanDirection, int, Bool);
static XawTextPosition Search(Widget, XawTextPosition, XawTextScanDirection,
			      XawTextBlock*);

/*
 * Prototypes
 */
static Piece *AllocNewPiece(AsciiSrcObject, Piece*);
static void BreakPiece(AsciiSrcObject, Piece*);
static Boolean CvtAsciiTypeToString(Display*, XrmValuePtr, Cardinal*,
				    XrmValuePtr, XrmValuePtr, XtPointer*);
static void CvtStringToAsciiType(XrmValuePtr, Cardinal*,
				 XrmValuePtr, XrmValuePtr);
static Piece *FindPiece(AsciiSrcObject, XawTextPosition, XawTextPosition*);
static void FreeAllPieces(AsciiSrcObject);
static FILE *InitStringOrFile(AsciiSrcObject, Bool);
static void LoadPieces(AsciiSrcObject, FILE*, char*);
static void RemoveOldStringOrFile(AsciiSrcObject, Bool);
static void RemovePiece(AsciiSrcObject, Piece*);
static String StorePiecesInString(AsciiSrcObject);
static Bool WriteToFile(String, String, unsigned);
static Bool WritePiecesToFile(AsciiSrcObject, String);
static void GetDefaultPieceSize(Widget, int, XrmValue*);

/*
 * More Prototypes
 */
#ifdef ASCII_DISK
Widget XawAsciiDiskSourceCreate(Widget, ArgList, Cardinal);
#endif
#ifdef ASCII_STRING
Widget XawStringSourceCreate(Widget, ArgList, Cardinal);
void XawTextSetLastPos(Widget, XawTextPosition);
#endif

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(AsciiSrcRec, ascii_src.field)
static XtResource resources[] = {
  {
    XtNstring,
    XtCString,
    XtRString,
    sizeof(char*),
    offset(string),
    XtRString,
    NULL
  },
  {
    XtNtype,
    XtCType,
    XtRAsciiType,
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
    (XtPointer)True
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
    offset(ascii_length),
    XtRImmediate,
    (XtPointer)MAGIC_VALUE
  },
#ifdef ASCII_DISK
  {
    XtNfile,
    XtCFile,
    XtRString,
    sizeof(String),
    offset(filename),
    XtRString,
    NULL
  },
#endif /* ASCII_DISK */
};
#undef offset


#define Superclass	(&textSrcClassRec)
AsciiSrcClassRec asciiSrcClassRec = {
  /* object */
  {
    (WidgetClass)Superclass,		/* superclass */
    "AsciiSrc",				/* class_name */
    sizeof(AsciiSrcRec),		/* widget_size */
    XawAsciiSrcClassInitialize,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawAsciiSrcInitialize,		/* initialize */
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
    XawAsciiSrcDestroy,			/* destroy */
    NULL,				/* resize */
    NULL,				/* expose */
    XawAsciiSrcSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    NULL,				/* set_values_almost */
    XawAsciiSrcGetValuesHook,		/* get_values_hook */
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
    ReadText,				/* Read */
    ReplaceText,			/* Replace */
    Scan,				/* Scan */
    Search,				/* Search */
    XtInheritSetSelection,		/* SetSelection */
    XtInheritConvertSelection,		/* ConvertSelection */
  },
  /* ascii_src */
  {
    NULL,				/* extension */
  },
};

WidgetClass asciiSrcObjectClass = (WidgetClass)&asciiSrcClassRec;

static XrmQuark Qstring, Qfile;

/*
 * Implementation
 */
/*
 * Function:
 *	XawAsciiSrcClassInitialize()
 *
 * Description:
 *	  Initializes the asciiSrcObjectClass and install the converters for
 *	AsciiType <-> String.
 */
static void
XawAsciiSrcClassInitialize(void)
{
    XawInitializeWidgetSet();
    Qstring = XrmPermStringToQuark(XtEstring);
    Qfile = XrmPermStringToQuark(XtEfile);
    XtAddConverter(XtRString, XtRAsciiType, CvtStringToAsciiType, NULL, 0);
    XtSetTypeConverter(XtRAsciiType, XtRString, CvtAsciiTypeToString,
		       NULL, 0, XtCacheNone, NULL);
}

/*
 * Function:
 *	XawAsciiSrcInitialize
 *
 * Parameters:
 *	request	 - widget requested by the argument list
 *	cnew	 - new widget with both resource and non resource values
 *	args	 - (unused)
 *	num_args - (unused)
 *
 * Description:
 *	Initializes the ascii src object.
 */
/*ARGSUSED*/
static void
XawAsciiSrcInitialize(Widget request, Widget cnew,
		      ArgList args, Cardinal *num_args)
{
    AsciiSrcObject src = (AsciiSrcObject)cnew;
    FILE *file;

    /*
     * Set correct flags (override resources) depending upon widget class
     */
    src->text_src.text_format = XawFmt8Bit;

#ifdef ASCII_DISK
    if (XtIsSubclass(XtParent(cnew), asciiDiskWidgetClass)) {
	src->ascii_src.type = XawAsciiFile;
	src->ascii_src.string = src->ascii_src.filename;
    }
#endif

#ifdef ASCII_STRING
    if (XtIsSubclass(XtParent(cnew), asciiStringWidgetClass)) {
	src->ascii_src.use_string_in_place = True;
	src->ascii_src.type = XawAsciiString;
    }
#endif

#ifdef OLDXAW
    src->ascii_src.changes = False;
#else
    src->text_src.changed = False;
#endif
    src->ascii_src.allocated_string = False;

    if (src->ascii_src.use_string_in_place && src->ascii_src.string == NULL)
	src->ascii_src.use_string_in_place = False;

    file = InitStringOrFile(src, src->ascii_src.type == XawAsciiFile);
    LoadPieces(src, file, NULL);

    if (file != NULL)
	fclose(file);
}

/*
 * Function:
 *	ReadText
 *
 * Parameters:
 *	w	- AsciiSource widget
 *	pos	- position of the text to retreive.
 *	text	- text block that will contain returned text
 *	length	- maximum number of characters to read
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
    AsciiSrcObject src = (AsciiSrcObject)w;
    XawTextPosition count, start;
    Piece *piece;
#ifndef OLDXAW
    XawTextAnchor *anchor;
    XawTextEntity *entity;
    XawTextPosition offset, end = pos + length;
    Bool state;

    end = XawMin(end, src->ascii_src.length);
    while ((state = XawTextSourceAnchorAndEntity(w, pos, &anchor, &entity)) &&
	(entity->flags & XAW_TENTF_HIDE))
	pos = anchor->position + entity->offset + entity->length;
    if (state == False ||
	!(entity->flags & XAW_TENTF_REPLACE)) {
	while (entity) {
	    offset = anchor->position + entity->offset;
	    if (offset >= end)
		break;
	    if (offset > pos &&
		(entity->flags & (XAW_TENTF_HIDE | XAW_TENTF_REPLACE))) {
		end = XawMin(end, offset);
		break;
	    }
	    if ((entity = entity->next) == NULL &&
		(anchor = XawTextSourceNextAnchor(w, anchor)) != NULL)
		entity = anchor->entities;
	}
    }
    else if (state && (entity->flags & XAW_TENTF_REPLACE) && pos < end) {
	XawTextBlock *block = (XawTextBlock*)entity->data;

	offset = anchor->position + entity->offset;
	end = XawMin(end, offset + block->length);
	if ((length = end - pos) < 0)
	    length = 0;
	text->length = length;
	text->format = XawFmt8Bit;
	if (length == 0) {
	    text->firstPos = end = offset + entity->length;
	    text->ptr = "";
	}
	else {
	    text->firstPos = pos;
	    text->ptr = block->ptr + (pos - offset);
	    if (pos + length < offset + block->length)
		end = pos + length;	/* there is data left to be read */
	    else
		end = offset + entity->length;
	}

	return (end);
    }

    if ((length = end - pos) < 0)
	length = 0;
#endif

    piece = FindPiece(src, pos, &start);
    text->firstPos = pos;
    text->ptr = piece->text + (pos - start);
    count = piece->used - (pos - start);
    text->length = Max(0, (length > count) ? count : length);
    text->format = XawFmt8Bit;

    return (pos + text->length);
}

/*
 * Function:
 *	ReplaceText
 *
 * Parameters:
 *	w	 - AsciiSource object
 *	startPos - ends of text that will be replaced
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
	    XawTextBlock *text)
{
    AsciiSrcObject src = (AsciiSrcObject)w;
    Piece *start_piece, *end_piece, *temp_piece;
    XawTextPosition start_first, end_first;
    int length, firstPos;

    /*
     * Editing a read only source is not allowed
     */
    if (src->text_src.edit_mode == XawtextRead)
	return (XawEditError);

    start_piece = FindPiece(src, startPos, &start_first);
    end_piece = FindPiece(src, endPos, &end_first);

#ifndef OLDXAW
    /*
     * This is a big hack, but I can't think about a clever way to know
     * if the character being moved forward has a negative lbearing.
     *
     */
    if (start_piece->used) {
	int i;

	for (i = 0; i < src->text_src.num_text; i++) {
	    int line;
	    TextWidget ctx = (TextWidget)src->text_src.text[i];

	    for (line = 0; line < ctx->text.lt.lines; line++)
		if (startPos < ctx->text.lt.info[line + 1].position)
		    break;
	    if (i < ctx->text.lt.lines &&
		startPos > ctx->text.lt.info[i].position) {
		AsciiSinkObject sink = (AsciiSinkObject)ctx->text.sink;
		XawTextAnchor *anchor;
		XawTextEntity *entity;
		XawTextProperty *property;
		XFontStruct *font;

		if (XawTextSourceAnchorAndEntity(w, startPos, &anchor, &entity) &&
		    (property = XawTextSinkGetProperty(ctx->text.sink,
						       entity->property)) != NULL &&
		    (property->mask & XAW_TPROP_FONT))
		    font = property->font;
		else
		    font = sink->ascii_sink.font;

		if (font->min_bounds.lbearing < 0) {
		    int lbearing = font->min_bounds.lbearing;
		    unsigned char c = *(unsigned char*)
			(start_piece->text + (startPos - start_first));

		    if (c == '\t' || c == '\n')
			c = ' ';
		    else if ((c & 0177) < XawSP || c == 0177) {
			if (sink->ascii_sink.display_nonprinting)
			    c = c > 0177 ? '\\' : c + '^';
			else
			    c = ' ';
		    }
		    if (font->per_char &&
			(c >= font->min_char_or_byte2 && c <= font->max_char_or_byte2))
			lbearing = font->per_char[c - font->min_char_or_byte2].lbearing;
		    if (lbearing < 0)
			_XawTextNeedsUpdating(ctx, startPos - 1, startPos);
		}
	    }
	}
    }


#endif

    /*
     * Remove Old Stuff
     */
    if (start_piece != end_piece) {
	temp_piece = start_piece->next;

	/*
	 * If empty and not the only piece then remove it.
	 */
	if (((start_piece->used = startPos - start_first) == 0)
	    && !(start_piece->next == NULL && start_piece->prev == NULL))
	    RemovePiece(src, start_piece);

	while (temp_piece != end_piece) {
	    temp_piece = temp_piece->next;
	    RemovePiece(src, temp_piece->prev);
	}

	end_piece->used -= endPos - end_first;
	if (end_piece->used != 0)
	    memmove(end_piece->text, end_piece->text + endPos - end_first,
		    (unsigned)end_piece->used);
    }
    else {		    /* We are fully in one piece */
	if ((start_piece->used -= endPos - startPos) == 0) {
	    if (!(start_piece->next == NULL && start_piece->prev == NULL))
		RemovePiece(src, start_piece);
	}
	else {
	    memmove(start_piece->text + (startPos - start_first),
		    start_piece->text + (endPos - start_first),
		    (unsigned)(start_piece->used - (startPos - start_first)));
	    if (src->ascii_src.use_string_in_place
		&& src->ascii_src.length - (endPos - startPos)
		< src->ascii_src.piece_size - 1)
		start_piece->text[src->ascii_src.length - (endPos - startPos)] =
		    '\0';
	}
    }

    src->ascii_src.length += -(endPos - startPos) + text->length;

    if ( text->length != 0) {
	/*
	 * Put in the New Stuff
	 */
	start_piece = FindPiece(src, startPos, &start_first);

	length = text->length;
	firstPos = text->firstPos;

	while (length > 0) {
	    char *ptr;
	    int fill;

	    if (src->ascii_src.use_string_in_place) {
		if (start_piece->used == src->ascii_src.piece_size - 1) {
		    /*
		     * If we are in ascii string emulation mode. Then the
		     *	string is not allowed to grow
		     */
		    start_piece->used = src->ascii_src.length =
			src->ascii_src.piece_size - 1;
		    start_piece->text[src->ascii_src.length] = '\0';
		    return (XawEditError);
		}
	    }

	    if (start_piece->used == src->ascii_src.piece_size) {
		BreakPiece(src, start_piece);
		start_piece = FindPiece(src, startPos, &start_first);
	    }

	    fill = Min((int)(src->ascii_src.piece_size - start_piece->used),
		       length);

	    ptr = start_piece->text + (startPos - start_first);
	    memmove(ptr + fill, ptr,
		    (unsigned)(start_piece->used - (startPos - start_first)));
	    memcpy(ptr, text->ptr + firstPos, (unsigned)fill);

	    startPos += fill;
	    firstPos += fill;
	    start_piece->used += fill;
	    length -= fill;
	}
    }

    if (src->ascii_src.use_string_in_place)
	start_piece->text[start_piece->used] = '\0';

#ifdef OLDXAW
    src->ascii_src.changes = True;
    XtCallCallbacks(w, XtNcallback, NULL);
#endif

    return (XawEditDone);
}

/*
 * Function:
 *	Scan
 *
 * Parameters:
 *	w	 - AsciiSource object
 *	position - position to start scanning
 *	type	 - type of thing to scan for
 *	dir	 - direction to scan
 *		   count - which occurance if this thing to search for.
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
    AsciiSrcObject src = (AsciiSrcObject)w;
    Piece *piece;
    XawTextPosition first, first_eol_position = 0;
    register char *ptr, *lim;
    register int cnt = count;
    register unsigned char c;

    if (dir == XawsdLeft) {
	if (position <= 0)
	    return (0);
	--position;
    }
    else if (position >= src->ascii_src.length)
	return (src->ascii_src.length);

    piece = FindPiece(src, position, &first);
    if (piece->used == 0)
	return (0);

    ptr = (position - first) + piece->text;

    if (dir == XawsdRight) {
	lim = piece->text + piece->used;
	switch (type) {
	    case XawstEOL:
	    case XawstParagraph:
	    case XawstWhiteSpace:
	    case XawstAlphaNumeric:
		for (; cnt > 0; cnt--) {
		    Bool non_space = False, first_eol = True;

		    /*CONSTCOND*/
		    while (True) {
			if (ptr >= lim) {
			    piece = piece->next;
			    if (piece == NULL)	/* End of text */
				return (src->ascii_src.length);
			    ptr = piece->text;
			    lim = piece->text + piece->used;
			}

			c = *ptr++;
			++position;

			if (type == XawstEOL) {
			    if (c == '\n')
				break;
			}
			else if (type == XawstAlphaNumeric) {
			    if (!isalnum(c)) {
				if (non_space)
				    break;
			    }
			    else
				non_space = True;
			}
			else if (type == XawstWhiteSpace) {
			    if (isspace(c)) {
				if (non_space)
				    break;
			    }
			    else
				non_space = True;
			}
			else {	/* XawstParagraph */
			    if (first_eol) {
				if (c == '\n') {
				    first_eol_position = position;
				    first_eol = False;
				}
			    }
			    else if (c == '\n')
				break;
			    else if (!isspace(c))
				first_eol = True;
			}
		    }
		}
		break;
	    case XawstPositions:
		position += count;
		return (position < src->ascii_src.length ?
			position : src->ascii_src.length);
	    case XawstAll:
		return (src->ascii_src.length);
	    default:
		break;
	}
	if (!include) {
	    if (type == XawstParagraph)
		position = first_eol_position;
	    if (count)
		--position;
	}
    }
    else {
	lim = piece->text;
	switch (type) {
	    case XawstEOL:
	    case XawstParagraph:
	    case XawstWhiteSpace:
	    case XawstAlphaNumeric:
		for (; cnt > 0; cnt--) {
		    Bool non_space = False, first_eol = True;

		    /*CONSTCOND*/
		    while (True) {
			if (ptr < lim) {
			    piece = piece->prev;
			    if (piece == NULL)	/* Begining of text */
				return (0);
			    ptr = piece->text + piece->used - 1;
			    lim = piece->text;
			}

			c = *ptr--;
			--position;

			if (type == XawstEOL) {
			    if (c == '\n')
				break;
			}
			else if (type == XawstAlphaNumeric) {
			    if (!isalnum(c)) {
				if (non_space)
				    break;
			    }
			    else
				non_space = True;
			}
			else if (type == XawstWhiteSpace) {
			    if (isspace(c)) {
				if (non_space)
				    break;
			    }
			    else
				non_space = True;
			}
			else {	/* XawstParagraph */
			    if (first_eol) {
				if (c == '\n') {
				    first_eol_position = position;
				    first_eol = False;
				}
			    }
			    else if (c == '\n')
				break;
			    else if (!isspace(c))
				first_eol = True;
			}
		    }
		}
		break;
	    case XawstPositions:
		position -= count - 1;
		return (position > 0 ? position : 0);
	    case XawstAll:
		return (0);
	    default:
		break;
	}
	if (!include) {
	    if (type == XawstParagraph)
		position = first_eol_position;
	    if (count)
		++position;
	}
	position++;
    }

    return (position);
}

/*
 * Function:
 *	Search
 *
 * Parameters:
 *	w	 - AsciiSource object
 *	position - the position to start scanning
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
    AsciiSrcObject src = (AsciiSrcObject)w;
    register int count = 0;
    register char *ptr, c;
    char *str;
    Piece *piece;
    char *buf;
    XawTextPosition first;
    int cnt, case_sensitive;

    if (dir == XawsdLeft) {
	if (position == 0)
	    return (XawTextSearchError);
	position--;
    }

    buf = XtMalloc((unsigned)sizeof(unsigned char) * text->length);
    memcpy(buf, text->ptr, (unsigned)text->length);
    piece = FindPiece(src, position, &first);
    ptr = (position - first) + piece->text;
    case_sensitive = text->firstPos;

    if (dir == XawsdRight) {
	str = buf;
	c = *str;
	/*CONSTCOND*/
	while (1) {
	    if (*ptr++ == c
		|| (case_sensitive && isalpha(c) && isalpha(ptr[-1])
		    && toupper(c) == toupper(ptr[-1]))) {
		if (++count == text->length)
		    break;
		c = *++str;
	    }
	    else if (count) {
		ptr -= count;
		str -= count;
		position -= count;
		count = 0;
		c = *str;

		if (ptr < piece->text) {
		    do {
			cnt = piece->text - ptr;
			piece = piece->prev;
			if (piece == NULL) {
			    XtFree(buf);
			    return (XawTextSearchError);
			}
			ptr = piece->text + piece->used - cnt;
		    } while (ptr < piece->text);
		}
	    }
	    position++;
	    if (ptr >= (piece->text + piece->used)) {
		do {
		    cnt = ptr - (piece->text + piece->used);
		    piece = piece->next;
		    if (piece == NULL) {
			XtFree(buf);
			return (XawTextSearchError);
		    }
		    ptr = piece->text + cnt;
		} while (ptr >= (piece->text + piece->used));
	    }
	}

	position -= text->length - 1;
    }
    else {
	str = buf + text->length - 1;
	c = *str;
	/*CONSTCOND*/
	while (1) {
	    if (*ptr-- == c
		|| (case_sensitive && isalpha(c) && isalpha(ptr[1])
		    && toupper(c) == toupper(ptr[1]))) {
		if (++count == text->length)
		    break;
		c = *--str;
	    }
	    else if (count) {
		ptr += count;
		str += count;
		position += count;
		count = 0;
		c = *str;

		if (ptr >= (piece->text + piece->used)) {
		    do {
			cnt = ptr - (piece->text + piece->used);
			piece = piece->next;
			if (piece == NULL) {
			    XtFree(buf);
			    return (XawTextSearchError);
			}
			ptr = piece->text + cnt;
		    } while (ptr >= (piece->text + piece->used));
		}
	    }
	    position--;
	    if (ptr < piece->text) {
		do {
		    cnt = piece->text - ptr;
		    piece = piece->prev;
		    if (piece == NULL) {
			XtFree(buf);
			return (XawTextSearchError);
		    }
		    ptr = piece->text + piece->used - cnt;
		} while (ptr < piece->text);
	    }
	}
    }

    XtFree(buf);

    return (position);
}

/*
 * Function:
 *	XawAsciiSrcSetValues
 *
 * Parameters:
 *	current  - current state of the widget
 *	request  - what was requested
 *	cnew	 - what the widget will become
 *	args	 - representation of changed resources
 *	num_args - number of resources that have changed
 *
 * Description:
 *	Sets the values for the AsciiSource.
 *
 * Returns:
 *	True if redisplay is needed
 */
static Boolean
XawAsciiSrcSetValues(Widget current, Widget request, Widget cnew,
		     ArgList args, Cardinal *num_args)
{
    AsciiSrcObject src = (AsciiSrcObject)cnew;
    AsciiSrcObject old_src = (AsciiSrcObject)current;
    Bool total_reset = False, string_set = False;
    FILE *file;
    unsigned int i;

    if (old_src->ascii_src.use_string_in_place
	!= src->ascii_src.use_string_in_place) {
	XtAppWarning(XtWidgetToApplicationContext(cnew),
		     "AsciiSrc: The XtNuseStringInPlace resource may "
		     "not be changed.");
	src->ascii_src.use_string_in_place =
	    old_src->ascii_src.use_string_in_place;
    }

    for (i = 0; i < *num_args ; i++)
	if (streq(args[i].name, XtNstring)) {
	    string_set = True;
	    break;
	}

    if (string_set || (old_src->ascii_src.type != src->ascii_src.type)) {
	RemoveOldStringOrFile(old_src, string_set); /* remove old info */
	file = InitStringOrFile(src, string_set);   /* Init new info */
	LoadPieces(src, file, NULL);   /* load new info into internal buffers */
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

    if (old_src->ascii_src.ascii_length != src->ascii_src.ascii_length)
	src->ascii_src.piece_size = src->ascii_src.ascii_length + 1;

    if (!total_reset &&
	old_src->ascii_src.piece_size != src->ascii_src.piece_size) {
	String string = StorePiecesInString(old_src);

	FreeAllPieces(old_src);
	LoadPieces(src, NULL, string);
	XtFree(string);
    }

    return (False);
}

/*
 * Function:
 *	XawAsciiSrcGetValuesHook
 *
 * Parameters:
 *	w	 - AsciiSource Widget
 *	args	 - argument list
 *	num_args - number of args
 *
 * Description:
 *	  This is a get values hook routine that sets the
 *		     values specific to the ascii source.
 */
static void
XawAsciiSrcGetValuesHook(Widget w, ArgList args, Cardinal *num_args)
{
    AsciiSrcObject src = (AsciiSrcObject)w;
    unsigned int i;

    if (src->ascii_src.type == XawAsciiString) {
	for (i = 0; i < *num_args ; i++)
	    if (streq(args[i].name, XtNstring)) {
		if (src->ascii_src.use_string_in_place)
		    *((char **)args[i].value) = src->ascii_src.first_piece->text;
		else if (XawAsciiSave(w))   /* If save sucessful */
		    *((char **)args[i].value) = src->ascii_src.string;
		break;
	    }
	}
    }

/*
 * Function:
 *	XawAsciiSrcDestroy
 *
 * Parameters:
 *	src - Ascii source object to free
 *
 * Description:
 *	Destroys an ascii source (frees all data)
 */
static void
XawAsciiSrcDestroy(Widget w)
{
    RemoveOldStringOrFile((AsciiSrcObject) w, True);
}

/*
 * Public routines
 */
/*
 * Function:
 *	XawAsciiSourceFreeString
 *
 * Parameters:
 *	w - AsciiSrc widget
 *
 * Description:
 *	  Frees the string returned by a get values call
 *		     on the string when the source is of type string.
 */
void
XawAsciiSourceFreeString(Widget w)
{
    AsciiSrcObject src = (AsciiSrcObject)w;

    /* If the src is really a multi, call the multi routine */
    if (XtIsSubclass(w, multiSrcObjectClass)) {
	_XawMultiSourceFreeString(w);
	return;
    }
    else if (!XtIsSubclass(w, asciiSrcObjectClass)) {
	XtErrorMsg("bad argument", "asciiSource", "XawError",
		   "XawAsciiSourceFreeString's parameter must be "
		   "an asciiSrc or multiSrc.",
		   NULL, NULL);
    }

    if (src->ascii_src.allocated_string && src->ascii_src.type != XawAsciiFile) {
	src->ascii_src.allocated_string = False;
	XtFree(src->ascii_src.string);
	src->ascii_src.string = NULL;
    }
}

/*
 * Function:
 *	XawAsciiSave
 *
 * Parameters:
 *	w - asciiSrc Widget
 *
 * Description:
 *	Saves all the pieces into a file or string as required.
 *
 * Returns:
 *	True if the save was successful
 */
Bool
XawAsciiSave(Widget w)
{
    AsciiSrcObject src = (AsciiSrcObject)w;

    /* If the src is really a multi, call the multi save */
    if (XtIsSubclass(w, multiSrcObjectClass ))
	return (_XawMultiSave(w));

    else if (!XtIsSubclass(w, asciiSrcObjectClass))
	XtErrorMsg("bad argument", "asciiSource", "XawError",
		   "XawAsciiSave's parameter must be an asciiSrc or multiSrc.",
		   NULL, NULL);

    /*
     * If using the string in place then there is no need to play games
     * to get the internal info into a readable string.
     */
    if (src->ascii_src.use_string_in_place)
	return (True);

    if (src->ascii_src.type == XawAsciiFile) {
#ifdef OLDXAW
	if (!src->ascii_src.changes)
#else
	if (!src->text_src.changed) 		/* No changes to save */
#endif
	    return (True);

	if (WritePiecesToFile(src, src->ascii_src.string) == False)
	    return (False);
    }
    else  {
	if (src->ascii_src.allocated_string == True)
	    XtFree(src->ascii_src.string);
	else
	    src->ascii_src.allocated_string = True;

	src->ascii_src.string = StorePiecesInString(src);
    }
#ifdef OLDXAW
    src->ascii_src.changes = False;
#else
    src->text_src.changed = False;
#endif

    return (True);
}

/*
 * Function:
 *	XawAsciiSaveAsFile
 *
 * Arguments:
 *	w    - AsciiSrc widget
 *	name - name of the file to save this file into
 *
 * Description:
 *	Save the current buffer as a file.
 *
 * Returns:
 *	True if the save was sucessful
 */
Bool
XawAsciiSaveAsFile(Widget w, _Xconst char *name)
{
    AsciiSrcObject src = (AsciiSrcObject)w;
    Bool ret;

    /* If the src is really a multi, call the multi save */

    if (XtIsSubclass( w, multiSrcObjectClass))
	return (_XawMultiSaveAsFile(w, name));

    else if (!XtIsSubclass(w, asciiSrcObjectClass))
	XtErrorMsg("bad argument", "asciiSource", "XawError",
		   "XawAsciiSaveAsFile's 1st parameter must be an "
		   "asciiSrc or multiSrc.",
		   NULL, NULL);

    if (src->ascii_src.type == XawAsciiFile)
	ret = WritePiecesToFile(src, (String)name);
    else {
	String string = StorePiecesInString(src);

	ret = WriteToFile(string, (String)name, src->ascii_src.length);
	XtFree(string);
    }

    return (ret);
}

/*
 * Function:
 *	XawAsciiSourceChanged
 *
 * Parameters:
 *	w - ascii source widget
 *
 * Description:
 *	Returns true if the source has changed since last saved.
 *
 * Returns:
 *	A Boolean (see description).
 */
Bool
XawAsciiSourceChanged(Widget w)
{
#ifdef OLDXAW
    if (XtIsSubclass(w, multiSrcObjectClass))
	return (((MultiSrcObject)w)->multi_src.changes);

    if (XtIsSubclass(w, asciiSrcObjectClass))
	return (((AsciiSrcObject)w)->ascii_src.changes);
#else
    if (XtIsSubclass(w, textSrcObjectClass))
	return (((TextSrcObject)w)->textSrc.changed);
#endif
    XtErrorMsg("bad argument", "asciiSource", "XawError",
	       "XawAsciiSourceChanged parameter must be an "
	       "asciiSrc or multiSrc.",
	       NULL, NULL);

    return (True);
}

/*
 * Private Functions
 */
static void
RemoveOldStringOrFile(AsciiSrcObject src, Bool checkString)
{
    FreeAllPieces(src);

    if (checkString && src->ascii_src.allocated_string) {
	XtFree(src->ascii_src.string);
	src->ascii_src.allocated_string = False;
	src->ascii_src.string = NULL;
    }
}

/*
 * Function:
 *	WriteToFile
 *
 * Parameters:
 *	string - string to write
 *	name   - the name of the file
 *
 * Description:
 *	Write the string specified to the begining of the file specified.
 *
 * Returns:
 *	returns True if sucessful, False otherwise
 */
static Bool
WriteToFile(String string, String name, unsigned length)
{
    int fd;

    if ((fd = creat(name, 0666)) == -1)
	return (False);

    if (write(fd, string, length) == -1) {
	close(fd);
	return (False);
    }

    if (close(fd) == -1)
	return (False);

    return (True);
}

/*
 * Function:
 *	WritePiecesToFile
 *
 * Parameters:
 *	src  - ascii source object
 *	name - name of the file
 *
 * Description:
 *	  Almost identical to WriteToFile, but only works for ascii src objects
 *	of type XawAsciiFile. This function avoids allocating temporary memory,
 *	what can be useful when editing very large files.
 *
 * Returns:
 *	returns True if sucessful, False otherwise
 */
static Bool
WritePiecesToFile(AsciiSrcObject src, String name)
{
    Piece *piece;
    int fd;

    if (src->ascii_src.data_compression) {
	Piece *tmp;

	piece = src->ascii_src.first_piece;
	while (piece) {
	    int bytes = src->ascii_src.piece_size - piece->used;

	    if (bytes > 0 && (tmp = piece->next) != NULL) {
		bytes = XawMin(bytes, tmp->used);
		memcpy(piece->text + piece->used, tmp->text, bytes);
		memmove(tmp->text, tmp->text + bytes, tmp->used - bytes);
		piece->used += bytes;
		if ((tmp->used -= bytes) == 0) {
		    RemovePiece(src, tmp);
		    continue;
		}
	    }
	    piece = piece->next;
	}
    }

    if ((fd = creat(name, 0666)) == -1)
	return (False);

    for (piece = src->ascii_src.first_piece; piece; piece = piece->next)
	if (write(fd, piece->text, piece->used) == -1) {
	    close(fd);
	    return (False);
	}

    if (close(fd) == -1)
	return (False);

    return (True);
}

/*
 * Function:
 *	StorePiecesInString
 *
 * Parameters:
 *	data - ascii pointer data
 *
 * Description:
 *	Store the pieces in memory into a standard ascii string.
 */
static String
StorePiecesInString(AsciiSrcObject src)
{
    String string;
    XawTextPosition first;
    Piece *piece;

    string = XtMalloc((unsigned)(src->ascii_src.length + 1));

    for (first = 0, piece = src->ascii_src.first_piece ; piece != NULL;
	 first += piece->used, piece = piece->next)
      memcpy(string + first, piece->text, (unsigned)piece->used);

    string[src->ascii_src.length] = '\0';

    /*
     * This will refill all pieces to capacity
     */
    if (src->ascii_src.data_compression) {
	FreeAllPieces(src);
	LoadPieces(src, NULL, string);
    }

    return (string);
}

/*
 * Function:
 *	InitStringOrFile
 *
 * Parameters:
 *	src - AsciiSource
 *
 * Description:
 *	Initializes the string or file.
 */
static FILE *
InitStringOrFile(AsciiSrcObject src, Bool newString)
{
    mode_t open_mode = 0;
    const char *fdopen_mode = NULL;
    int fd;
    FILE *file;

    if (src->ascii_src.type == XawAsciiString) {
	if (src->ascii_src.string == NULL)
	    src->ascii_src.length = 0;

	else if (!src->ascii_src.use_string_in_place) {
	    src->ascii_src.string = XtNewString(src->ascii_src.string);
	    src->ascii_src.allocated_string = True;
	    src->ascii_src.length = strlen(src->ascii_src.string);
	}

	if (src->ascii_src.use_string_in_place) {
	    if (src->ascii_src.string != NULL)
	    src->ascii_src.length = strlen(src->ascii_src.string);
	    /* In case the length resource is incorrectly set */
	    if (src->ascii_src.length > src->ascii_src.ascii_length)
		src->ascii_src.ascii_length = src->ascii_src.length;

	    if (src->ascii_src.ascii_length == MAGIC_VALUE)
		src->ascii_src.piece_size = src->ascii_src.length;
	    else
		src->ascii_src.piece_size = src->ascii_src.ascii_length + 1;
	}

	return (NULL);
    }

    /*
     * type is XawAsciiFile
     */
    src->ascii_src.is_tempfile = False;

    switch (src->text_src.edit_mode) {
	case XawtextRead:
	    if (src->ascii_src.string == NULL)
		XtErrorMsg("NoFile", "asciiSourceCreate", "XawError",
			   "Creating a read only disk widget and no file specified.",
			   NULL, NULL);
	    open_mode = O_RDONLY;
	    fdopen_mode = "r";
	    break;
	case XawtextAppend:
	case XawtextEdit:
	    if (src->ascii_src.string == NULL) {
		src->ascii_src.string = "*ascii-src*";
		src->ascii_src.is_tempfile = True;
	    }
	    else {
/* O_NOFOLLOW is a FreeBSD & Linux extension */
#ifdef O_NOFOLLOW
		open_mode = O_RDWR | O_NOFOLLOW;
#else
		open_mode = O_RDWR; /* unsafe; subject to race conditions */
#endif /* O_NOFOLLOW */
		fdopen_mode = "r+";
	    }
	    break;
	default:
	    XtErrorMsg("badMode", "asciiSourceCreate", "XawError",
		       "Bad editMode for ascii source; must be Read, "
		       "Append or Edit.",
		       NULL, NULL);
    }

    /* If is_tempfile, allocate a private copy of the text
     * Unlikely to be changed, just to set allocated_string */
    if (newString || src->ascii_src.is_tempfile) {
	src->ascii_src.string = XtNewString(src->ascii_src.string);
	src->ascii_src.allocated_string = True;
    }

    if (!src->ascii_src.is_tempfile) {
	if ((fd = open(src->ascii_src.string, open_mode, 0666)) != -1) {
	    if ((file = fdopen(fd, fdopen_mode))) {
		(void)fseek(file, 0, SEEK_END);
		src->ascii_src.length = (XawTextPosition)ftell(file);
		return (file);
	    }
	    else
		close(fd);
	}
	{
	    String params[2];
	    Cardinal num_params = 2;

	    params[0] = src->ascii_src.string;
	    params[1] = strerror(errno);
	    XtAppWarningMsg(XtWidgetToApplicationContext((Widget)src),
			    "openError", "asciiSourceCreate", "XawWarning",
			    "Cannot open file %s; %s", params, &num_params);
	}
    }
    src->ascii_src.length = 0;
    return (NULL);
}

static void
LoadPieces(AsciiSrcObject src, FILE *file, char *string)
{
    char *ptr;
    Piece *piece = NULL;
    XawTextPosition left;

    if (string == NULL) {
	if (src->ascii_src.type == XawAsciiFile) {
	    if (src->ascii_src.length != 0) {
		int len;

		left = 0;
		fseek(file, 0, SEEK_SET);
		while (left < src->ascii_src.length) {
		    ptr = XtMalloc((unsigned)src->ascii_src.piece_size);
		    if ((len = fread(ptr, sizeof(unsigned char),
				     (size_t)src->ascii_src.piece_size, file)) < 0)
			XtErrorMsg("readError", "asciiSourceCreate", "XawError",
				   "fread returned error.", NULL, NULL);
		    piece = AllocNewPiece(src, piece);
		    piece->text = ptr;
		    piece->used = XawMin(len, src->ascii_src.piece_size);
		    left += piece->used;
		}
	    }
	    else {
		piece = AllocNewPiece(src, NULL);
		piece->text = XtMalloc((unsigned)src->ascii_src.piece_size);
		piece->used = 0;
	    }
	    return;
	}
	else
	    string = src->ascii_src.string;
    }

    if (src->ascii_src.use_string_in_place) {
	piece = AllocNewPiece(src, piece);
	piece->used = XawMin(src->ascii_src.length, src->ascii_src.piece_size);
	piece->text = src->ascii_src.string;
	return;
    }

    ptr = string;
    left = src->ascii_src.length;
    do {
	piece = AllocNewPiece(src, piece);

	piece->text = XtMalloc((unsigned)src->ascii_src.piece_size);
	piece->used = XawMin(left, src->ascii_src.piece_size);
	if (piece->used != 0)
	    memcpy(piece->text, ptr, (unsigned)piece->used);

	left -= piece->used;
	ptr += piece->used;
    } while (left > 0);
}

/*
 * Function:
 *	AllocNewPiece
 *
 * Parameters:
 *	src - AsciiSrc Widget
 *	prev - piece just before this one, or NULL
 *
 * Description:
 *	Allocates a new piece of memory.
 *
 * Returns:
 *	The allocated piece
 */
static Piece *
AllocNewPiece(AsciiSrcObject src, Piece *prev)
{
    Piece *piece = XtNew(Piece);

    if (prev == NULL) {
	src->ascii_src.first_piece = piece;
	piece->next = NULL;
    }
    else  {
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
 *	src - AsciiSrc Widget
 *
 * Description:
 *	Frees all the pieces.
 */
static void
FreeAllPieces(AsciiSrcObject src)
{
    Piece *next, * first = src->ascii_src.first_piece;

#ifdef DEBUG
    if (first->prev != NULL)
	printf("Xaw AsciiSrc Object: possible memory leak in FreeAllPieces().\n");
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
RemovePiece(AsciiSrcObject src, Piece *piece)
{
    if (piece->prev == NULL)
	src->ascii_src.first_piece = piece->next;
    else
	piece->prev->next = piece->next;

    if (piece->next != NULL)
	piece->next->prev = piece->prev;

    if (!src->ascii_src.use_string_in_place)
	XtFree(piece->text);

    XtFree((char *)piece);
}

/*
 * Function:
 *	FindPiece
 *
 * Parameters:
 *	src	 - AsciiSrc Widget
 *	position - position that we are searching for
 *	first	 - position of the first character in this piece (return)
 *
 * Description:
 *	Finds the piece containing the position indicated.
 *
 * Returns:
 *	the piece that contains this position
 */
static Piece *
FindPiece(AsciiSrcObject src, XawTextPosition position, XawTextPosition *first)
{
    Piece *old_piece, *piece;
    XawTextPosition temp;

    for (old_piece = NULL, piece = src->ascii_src.first_piece, temp = 0;
	piece; old_piece = piece, piece = piece->next)
	if ((temp += piece->used) > position) {
	    *first = temp - piece->used;
	    return (piece);
	}

    *first = temp - (old_piece ? old_piece->used : 0);

    return (old_piece);	/* if we run off the end the return the last piece */
}

/*
 * Function:
 *	BreakPiece
 *
 * Parameters:
 *	src - AsciiSrc Widget
 *	piece - piece to break
 *
 * Description:
 *	Breaks a full piece into two new pieces.
 */
#define HALF_PIECE (src->ascii_src.piece_size >> 1)
static void
BreakPiece(AsciiSrcObject src, Piece *piece)
{
    Piece *cnew = AllocNewPiece(src, piece);

    cnew->text = XtMalloc((unsigned)src->ascii_src.piece_size);
    memcpy(cnew->text, piece->text + HALF_PIECE,
	   (unsigned)(src->ascii_src.piece_size - HALF_PIECE));
    piece->used = HALF_PIECE;
    cnew->used = src->ascii_src.piece_size - HALF_PIECE;
}

/*ARGSUSED*/
static void
CvtStringToAsciiType(XrmValuePtr args, Cardinal *num_args,
		     XrmValuePtr fromVal, XrmValuePtr toVal)
{
    static XawAsciiType type;
    XrmQuark q;
    char name[7];

    XmuNCopyISOLatin1Lowered(name, (char *)fromVal->addr, sizeof(name));
    q = XrmStringToQuark(name);

    if (q == Qstring)
	type = XawAsciiString;
    else if (q == Qfile)
	type = XawAsciiFile;
    else  {
	toVal->size = 0;
	toVal->addr = NULL;
	XtStringConversionWarning((char *)fromVal->addr, XtRAsciiType);
    }

    toVal->size = sizeof(XawAsciiType);
    toVal->addr = (XPointer)&type;
}

/*ARGSUSED*/
static Boolean
CvtAsciiTypeToString(Display *dpy, XrmValuePtr args, Cardinal *num_args,
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

    if (pagesize == NULL) {
	pagesize = (XPointer)((long)_XawGetPageSize());
	if (pagesize < (XPointer)BUFSIZ)
	    pagesize = (XPointer)BUFSIZ;
    }

    value->addr = (XPointer)&pagesize;
}

#if (defined(ASCII_STRING) || defined(ASCII_DISK))
#  include <X11/Xaw/Cardinals.h>
#endif

#ifdef ASCII_STRING
/*
 * Compatability functions.
 */
/*
 * Function:
 *	AsciiStringSourceCreate
 *
 * Parameters:
 *	parent	 - widget that will own this source
 *	args	 - the argument list
 *	num_args - ""
 *
 * Description:
 *	Creates a string source.
 *
 * Returns:
 *	A pointer to the new text source.
 */
Widget
XawStringSourceCreate(Widget parent, ArgList args, Cardinal num_args)
{
    XawTextSource src;
    ArgList ascii_args;
    Arg temp[2];

    XtSetArg(temp[0], XtNtype, XawAsciiString);
    XtSetArg(temp[1], XtNuseStringInPlace, True);
    ascii_args = XtMergeArgLists(temp, TWO, args, num_args);

    src = XtCreateWidget("genericAsciiString", asciiSrcObjectClass, parent,
			 ascii_args, num_args + TWO);
    XtFree((char *)ascii_args);

    return (src);
}

/*
 * This is hacked up to try to emulate old functionality, it
 * may not work, as I have not old code to test it on.
 *
 * Chris D. Peterson  8/31/89.
 */
void
XawTextSetLastPos(Widget w, XawTextPosition lastPos)
{
    AsciiSrcObject src = (AsciiSrcObject)XawTextGetSource(w);

    src->ascii_src.piece_size = lastPos;
}
#endif /* ASCII_STRING */

#ifdef ASCII_DISK
/*
 * Function:
 *	AsciiDiskSourceCreate
 *
 * Parameters:
 *	parent	 - widget that will own this source
 *	args	 - argument list
 *	num_args - ""
 *
 * Description:
 *	Creates a disk source.
 *
 * Returns:
 *	A pointer to the new text source
 */
Widget
XawDiskSourceCreate(Widget parent, ArgList args, Cardinal num_args)
{
    XawTextSource src;
    ArgList ascii_args;
    Arg temp[1];
    int i;

    XtSetArg(temp[0], XtNtype, XawAsciiFile);
    ascii_args = XtMergeArgLists(temp, ONE, args, num_args);
    num_args++;

    for (i = 0; i < num_args; i++)
	if (streq(ascii_args[i].name, XtNfile)
	    || streq(ascii_args[i].name, XtCFile))
	    ascii_args[i].name = XtNstring;

    src = XtCreateWidget("genericAsciiDisk", asciiSrcObjectClass, parent,
			 ascii_args, num_args);
    XtFree((char *)ascii_args);

    return (src);
}
#endif /* ASCII_DISK */
