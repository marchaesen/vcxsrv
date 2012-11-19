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

char **
XListFonts(
register Display *dpy,
_Xconst char *pattern,  /* null-terminated */
int maxNames,
int *actualCount)	/* RETURN */
{
    register long nbytes;
    register unsigned i;
    register int length;
    char **flist;
    char *ch;
    xListFontsReply rep;
    register xListFontsReq *req;
    register long rlen;

    LockDisplay(dpy);
    GetReq(ListFonts, req);
    req->maxNames = maxNames;
    nbytes = req->nbytes = pattern ? strlen (pattern) : 0;
    req->length += (nbytes + 3) >> 2;
    _XSend (dpy, pattern, nbytes);
    /* use _XSend instead of Data, since following _XReply will flush buffer */

    if (!_XReply (dpy, (xReply *)&rep, 0, xFalse)) {
	*actualCount = 0;
	UnlockDisplay(dpy);
	SyncHandle();
	return (char **) NULL;
    }

    if (rep.nFonts) {
	flist = (char **)Xmalloc ((unsigned)rep.nFonts * sizeof(char *));
	rlen = rep.length << 2;
	ch = (char *) Xmalloc((unsigned) (rlen + 1));
	    /* +1 to leave room for last null-terminator */

	if ((! flist) || (! ch)) {
	    if (flist) Xfree((char *) flist);
	    if (ch) Xfree(ch);
	    _XEatData(dpy, (unsigned long) rlen);
	    *actualCount = 0;
	    UnlockDisplay(dpy);
	    SyncHandle();
	    return (char **) NULL;
	}

	_XReadPad (dpy, ch, rlen);
	/*
	 * unpack into null terminated strings.
	 */
	length = *(unsigned char *)ch;
	*ch = 1; /* make sure it is non-zero for XFreeFontNames */
	for (i = 0; i < rep.nFonts; i++) {
	    flist[i] = ch + 1;  /* skip over length */
	    ch += length + 1;  /* find next length ... */
	    length = *(unsigned char *)ch;
	    *ch = '\0';  /* and replace with null-termination */
	}
    }
    else flist = (char **) NULL;
    *actualCount = rep.nFonts;
    UnlockDisplay(dpy);
    SyncHandle();
    return (flist);
}

int
XFreeFontNames(char **list)
{
	if (list) {
		if (!*(list[0]-1)) { /* from ListFontsWithInfo */
			register char **names;
			for (names = list+1; *names; names++)
				Xfree (*names);
		}
		Xfree (list[0]-1);
		Xfree ((char *)list);
	}
	return 1;
}
