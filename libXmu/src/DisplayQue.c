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
 * Author:  Jim Fulton, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <X11/Xmu/DisplayQue.h>

/*
 * Prototypes
 */
static int _DQCloseDisplay(Display*, XPointer);

#define CallCloseCallback(q,e) (void) (*((q)->closefunc)) ((q), (e))
#define CallFreeCallback(q) (void) (*((q)->freefunc)) ((q))

/*
 * XmuDQCreate - create a display queue
 */
XmuDisplayQueue *
XmuDQCreate(XmuCloseDisplayQueueProc closefunc,
	    XmuFreeDisplayQueueProc freefunc,
	    XPointer data)
{
    XmuDisplayQueue *q = malloc (sizeof (XmuDisplayQueue));
    if (q) {
	q->nentries = 0;
	q->head = q->tail = NULL;
	q->closefunc = closefunc;
	q->freefunc = freefunc;
	q->data = data;
    }
    return q;
}


/*
 * XmuDQDestroy - free all storage associated with this display queue,
 * optionally invoking the close callbacks.
 */

Bool
XmuDQDestroy(XmuDisplayQueue *q, Bool docallbacks)
{
    XmuDisplayQueueEntry *e = q->head;

    while (e) {
	XmuDisplayQueueEntry *nexte = e->next;
	if (docallbacks && q->closefunc) CallCloseCallback (q, e);
	free (e);
	e = nexte;
    }
    free (q);
    return True;
}


/*
 * XmuDQLookupDisplay - finds the indicated display on the given queue
 */
XmuDisplayQueueEntry *
XmuDQLookupDisplay(XmuDisplayQueue *q, Display *dpy)
{
    XmuDisplayQueueEntry *e;

    for (e = q->head; e; e = e->next) {
	if (e->display == dpy) return e;
    }
    return NULL;
}


/*
 * XmuDQAddDisplay - add the specified display to the queue; set data as a
 * convenience.  Does not ensure that dpy hasn't already been added.
 */
XmuDisplayQueueEntry *
XmuDQAddDisplay(XmuDisplayQueue *q, Display *dpy, XPointer data)
{
    XmuDisplayQueueEntry *e;

    if (!(e = malloc (sizeof (XmuDisplayQueueEntry)))) {
	return NULL;
    }
    if (!(e->closehook = XmuAddCloseDisplayHook (dpy, _DQCloseDisplay,
						 (XPointer) q))) {
	free (e);
	return NULL;
    }

    e->display = dpy;
    e->next = NULL;
    e->data = data;

    if (q->tail) {
	q->tail->next = e;
	e->prev = q->tail;
    } else {
	q->head = e;
	e->prev = NULL;
    }
    q->tail = e;
    q->nentries++;
    return e;
}


/*
 * XmuDQRemoveDisplay - remove the specified display from the queue
 */
Bool
XmuDQRemoveDisplay(XmuDisplayQueue *q, Display *dpy)
{
    XmuDisplayQueueEntry *e;

    for (e = q->head; e; e = e->next) {
	if (e->display == dpy) {
	    if (q->head == e)
	      q->head = e->next;	/* if at head, then bump head */
	    else
	      e->prev->next = e->next;	/* else splice out */
	    if (q->tail == e)
	      q->tail = e->prev;	/* if at tail, then bump tail */
	    else
	      e->next->prev = e->prev;	/* else splice out */
	    (void) XmuRemoveCloseDisplayHook (dpy, e->closehook,
					      _DQCloseDisplay, (XPointer) q);
	    free (e);
	    q->nentries--;
	    return True;
	}
    }
    return False;
}


/*****************************************************************************
 *			       private functions                             *
 *****************************************************************************/

/*
 * _DQCloseDisplay - upcalled from CloseHook to notify this queue; remove the
 * display when finished
 */
static int
_DQCloseDisplay(Display *dpy, XPointer arg)
{
    XmuDisplayQueue *q = (XmuDisplayQueue *) arg;
    XmuDisplayQueueEntry *e;

    for (e = q->head; e; e = e->next) {
	if (e->display == dpy) {
	    if (q->closefunc) CallCloseCallback (q, e);
	    (void) XmuDQRemoveDisplay (q, dpy);
	    if (q->nentries == 0 && q->freefunc) CallFreeCallback (q);
	    return 1;
	}
    }

    return 0;
}
