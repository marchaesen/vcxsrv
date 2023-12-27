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
#include <X11/Xos.h>
#include <X11/Xmu/CvtCache.h>
#include <stdlib.h>

/*
 * Prototypes
 */
static int _CloseDisplay(XmuDisplayQueue*, XmuDisplayQueueEntry*);
static int _FreeCCDQ(XmuDisplayQueue*);
static void _InitializeCvtCache(XmuCvtCache*);

/*
 * Initialization
 */
static XmuDisplayQueue *dq = NULL;


/*
 * internal utility callbacks
 */

static int
_FreeCCDQ(XmuDisplayQueue *q)
{
    XmuDQDestroy (dq, False);
    dq = NULL;
    return (0);
}


static int
_CloseDisplay(XmuDisplayQueue *q, XmuDisplayQueueEntry *e)
{
    XmuCvtCache *c;

    if (e && (c = (XmuCvtCache *)(e->data))) {
	_XmuStringToBitmapFreeCache (c);
	/* insert calls to free any cached memory */

    }
    return 0;
}

static void
_InitializeCvtCache(register XmuCvtCache *c)
{
    _XmuStringToBitmapInitCache (c);
    /* insert calls to init any cached memory */
}


/*
 * XmuCCLookupDisplay - return the cache entry for the indicated display;
 * initialize the cache if necessary
 */
XmuCvtCache *
_XmuCCLookupDisplay(Display *dpy)
{
    XmuDisplayQueueEntry *e;

    /*
     * If no displays have been added before this, create the display queue.
     */
    if (!dq) {
	dq = XmuDQCreate (_CloseDisplay, _FreeCCDQ, NULL);
	if (!dq) return NULL;
    }

    /*
     * See if the display is already there
     */
    e = XmuDQLookupDisplay (dq, dpy);	/* see if it's there */
    if (!e) {				/* else create it */
	XmuCvtCache *c = malloc (sizeof (XmuCvtCache));
	if (!c) return NULL;

	/*
	 * Add the display to the queue
	 */
	e = XmuDQAddDisplay (dq, dpy, (XPointer) c);
	if (!e) {
	    free (c);
	    return NULL;
	}

	/*
	 * initialize fields in cache
	 */
	_InitializeCvtCache (c);
    }

    /*
     * got it
     */
    return (XmuCvtCache *)(e->data);
}


