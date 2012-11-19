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

XTimeCoord *XGetMotionEvents(
    register Display *dpy,
    Window w,
    Time start,
    Time stop,
    int *nEvents)  /* RETURN */
{
    xGetMotionEventsReply rep;
    register xGetMotionEventsReq *req;
    XTimeCoord *tc = NULL;
    long nbytes;
    LockDisplay(dpy);
    GetReq(GetMotionEvents, req);
    req->window = w;
/* XXX is this right for all machines? */
    req->start = start;
    req->stop  = stop;
    if (!_XReply (dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
        SyncHandle();
	return (NULL);
	}

    if (rep.nEvents) {
	if (! (tc = (XTimeCoord *)
	       Xmalloc( (unsigned)
		       (nbytes = (long) rep.nEvents * sizeof(XTimeCoord))))) {
	    _XEatData (dpy, (unsigned long) nbytes);
	    UnlockDisplay(dpy);
	    SyncHandle();
	    return (NULL);
	}
    }

    *nEvents = rep.nEvents;
    nbytes = SIZEOF (xTimecoord);
    {
	register XTimeCoord *tcptr;
	register int i;
	xTimecoord xtc;

	for (i = rep.nEvents, tcptr = tc; i > 0; i--, tcptr++) {
	    _XRead (dpy, (char *) &xtc, nbytes);
	    tcptr->time = xtc.time;
	    tcptr->x    = cvtINT16toShort (xtc.x);
	    tcptr->y    = cvtINT16toShort (xtc.y);
	}
    }

    UnlockDisplay(dpy);
    SyncHandle();
    return (tc);
}

