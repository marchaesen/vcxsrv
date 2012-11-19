/*

Copyright 1986, 1998  The Open Group

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
#include "Xlibint.h"

#if defined(XF86BIGFONT) && !defined(MUSTCOPY)
#define USE_XF86BIGFONT
#endif
#ifdef USE_XF86BIGFONT
extern void _XF86BigfontFreeFontMetrics(
    XFontStruct*	/* fs */
);
#endif

char **XListFontsWithInfo(
register Display *dpy,
_Xconst char *pattern,  /* null-terminated */
int maxNames,
int *actualCount,	/* RETURN */
XFontStruct **info)	/* RETURN */
{
    register long nbytes;
    register int i;
    register XFontStruct *fs;
    register int size = 0;
    XFontStruct *finfo = NULL;
    char **flist = NULL;
    xListFontsWithInfoReply reply;
    register xListFontsReq *req;
    int j;

    LockDisplay(dpy);
    GetReq(ListFontsWithInfo, req);
    req->maxNames = maxNames;
    nbytes = req->nbytes = pattern ? strlen (pattern) : 0;
    req->length += (nbytes + 3) >> 2;
    _XSend (dpy, pattern, nbytes);
    /* use _XSend instead of Data, since subsequent _XReply will flush buffer */

    for (i = 0; ; i++) {
	if (!_XReply (dpy, (xReply *) &reply,
		      ((SIZEOF(xListFontsWithInfoReply) -
			SIZEOF(xGenericReply)) >> 2), xFalse)) {
	    for (j=(i-1); (j >= 0); j--) {
		Xfree(flist[j]);
		if (finfo[j].properties) Xfree((char *) finfo[j].properties);
	    }
	    if (flist) Xfree((char *) flist);
	    if (finfo) Xfree((char *) finfo);
	    UnlockDisplay(dpy);
	    SyncHandle();
	    return ((char **) NULL);
	}
	if (reply.nameLength == 0)
	    break;
	if ((i + reply.nReplies) >= size) {
	    size = i + reply.nReplies + 1;

	    if (finfo) {
		XFontStruct * tmp_finfo = (XFontStruct *)
		    Xrealloc ((char *) finfo,
			      (unsigned) (sizeof(XFontStruct) * size));
		char ** tmp_flist = (char **)
		    Xrealloc ((char *) flist,
			      (unsigned) (sizeof(char *) * (size+1)));

		if ((! tmp_finfo) || (! tmp_flist)) {
		    /* free all the memory that we allocated */
		    for (j=(i-1); (j >= 0); j--) {
			Xfree(flist[j]);
			if (finfo[j].properties)
			    Xfree((char *) finfo[j].properties);
		    }
		    if (tmp_flist) Xfree((char *) tmp_flist);
		    else Xfree((char *) flist);
		    if (tmp_finfo) Xfree((char *) tmp_finfo);
		    else Xfree((char *) finfo);
		    goto clearwire;
		}
		finfo = tmp_finfo;
		flist = tmp_flist;
	    }
	    else {
		if (! (finfo = (XFontStruct *)
		       Xmalloc((unsigned) (sizeof(XFontStruct) * size))))
		    goto clearwire;
		if (! (flist = (char **)
		       Xmalloc((unsigned) (sizeof(char *) * (size+1))))) {
		    Xfree((char *) finfo);
		    goto clearwire;
		}
	    }
	}
	fs = &finfo[i];

	fs->ext_data 		= NULL;
	fs->per_char		= NULL;
	fs->fid 		= None;
	fs->direction 		= reply.drawDirection;
	fs->min_char_or_byte2	= reply.minCharOrByte2;
	fs->max_char_or_byte2 	= reply.maxCharOrByte2;
	fs->min_byte1 		= reply.minByte1;
	fs->max_byte1 		= reply.maxByte1;
	fs->default_char	= reply.defaultChar;
	fs->all_chars_exist 	= reply.allCharsExist;
	fs->ascent 		= cvtINT16toInt (reply.fontAscent);
	fs->descent 		= cvtINT16toInt (reply.fontDescent);

#ifdef MUSTCOPY
	{
	    xCharInfo *xcip;

	    xcip = (xCharInfo *) &reply.minBounds;
	    fs->min_bounds.lbearing = xcip->leftSideBearing;
	    fs->min_bounds.rbearing = xcip->rightSideBearing;
	    fs->min_bounds.width = xcip->characterWidth;
	    fs->min_bounds.ascent = xcip->ascent;
	    fs->min_bounds.descent = xcip->descent;
	    fs->min_bounds.attributes = xcip->attributes;

	    xcip = (xCharInfo *) &reply.maxBounds;
	    fs->max_bounds.lbearing = xcip->leftSideBearing;
	    fs->max_bounds.rbearing = xcip->rightSideBearing;
	    fs->max_bounds.width = xcip->characterWidth;
	    fs->max_bounds.ascent = xcip->ascent;
	    fs->max_bounds.descent = xcip->descent;
	    fs->max_bounds.attributes = xcip->attributes;
	}
#else
	/* XXX the next two statements won't work if short isn't 16 bits */
	fs->min_bounds = * (XCharStruct *) &reply.minBounds;
	fs->max_bounds = * (XCharStruct *) &reply.maxBounds;
#endif /* MUSTCOPY */

	fs->n_properties = reply.nFontProps;
	if (fs->n_properties > 0) {
	    nbytes = reply.nFontProps * sizeof(XFontProp);
	    if (! (fs->properties = (XFontProp *) Xmalloc((unsigned) nbytes)))
		goto badmem;
	    nbytes = reply.nFontProps * SIZEOF(xFontProp);
	    _XRead32 (dpy, (long *)fs->properties, nbytes);

	} else
	    fs->properties = NULL;

	j = reply.nameLength + 1;
	if (!i)
	    j++; /* make first string 1 byte longer, to match XListFonts */
	flist[i] = (char *) Xmalloc ((unsigned int) j);
	if (! flist[i]) {
	    if (finfo[i].properties) Xfree((char *) finfo[i].properties);
	    nbytes = (reply.nameLength + 3) & ~3;
	    _XEatData(dpy, (unsigned long) nbytes);
	    goto badmem;
	}
	if (!i) {
	    *flist[0] = 0; /* zero to distinguish from XListFonts */
	    flist[0]++;
	}
	flist[i][reply.nameLength] = '\0';
	_XReadPad (dpy, flist[i], (long) reply.nameLength);
    }
    *info = finfo;
    *actualCount = i;
    if (flist)
	flist[i] = NULL; /* required in case XFreeFontNames is called */
    UnlockDisplay(dpy);
    SyncHandle();
    return (flist);


  badmem:
    /* Free all memory allocated by this function. */
    for (j=(i-1); (j >= 0); j--) {
	Xfree(flist[j]);
	if (finfo[j].properties) Xfree((char *) finfo[j].properties);
    }
    if (flist) Xfree((char *) flist);
    if (finfo) Xfree((char *) finfo);

  clearwire:
    /* Clear the wire. */
    do {
	if (reply.nFontProps)
	    _XEatData(dpy, (unsigned long)
		      (reply.nFontProps * SIZEOF(xFontProp)));
	nbytes = (reply.nameLength + 3) & ~3;
	_XEatData(dpy, (unsigned long) nbytes);
    }
    while (_XReply(dpy,(xReply *) &reply, ((SIZEOF(xListFontsWithInfoReply) -
					    SIZEOF(xGenericReply)) >> 2),
		   xFalse) && (reply.nameLength != 0));

    UnlockDisplay(dpy);
    SyncHandle();
    return (char **) NULL;
}

int
XFreeFontInfo (
    char **names,
    XFontStruct *info,
    int actualCount)
{
	register int i;
	if (names) {
		Xfree (names[0]-1);
		for (i = 1; i < actualCount; i++) {
			Xfree (names[i]);
		}
		Xfree((char *) names);
	}
	if (info) {
		for (i = 0; i < actualCount; i++) {
			if (info[i].per_char)
#ifdef USE_XF86BIGFONT
				_XF86BigfontFreeFontMetrics(&info[i]);
#else
				Xfree ((char *) info[i].per_char);
#endif
			if (info[i].properties)
				Xfree ((char *) info[i].properties);
			}
		Xfree((char *) info);
	}
	return 1;
}
