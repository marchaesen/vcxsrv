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
 * CloseDisplayHook package - provide callback on XCloseDisplay
 *
 * *
 * Author:  Jim Fulton, MIT X Consortium
 *
 *
 *			      Public Entry Points
 *
 * CloseHook XmuAddCloseDisplayHook (dpy, func, arg)
 *     Display *dpy;
 *     XmuCloseHookProc func;
 *     XPointer arg;
 *
 * Bool XmuRemoveCloseDisplayHook (dpy, hook, func, arg)
 *     Display *dpy;
 *     CloseHook hook;
 *     XmuCloseHookProc func;
 *     XPointer arg;
 *
 * Bool XmuLookupCloseDisplayHook (dpy, hook, func, arg)
 *     Display *dpy;
 *     CloseHook hook;
 *     XmuCloseHookProc func;
 *     XPointer arg;
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>					/* for NULL */
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xmu/CloseHook.h>
#include <stdlib.h>

/*
 *				 Private data
 *
 * This is a list of display entries, each of which contains a list of callback
 * records.
 */

typedef struct _CallbackRec {
    struct _CallbackRec *next;		/* next link in chain */
    XmuCloseHookProc func;		/* function to call */
    XPointer arg;			/* argument to pass with function */
} CallbackRec;


typedef struct _DisplayEntry {
    struct _DisplayEntry *next;		/* next link in chain */
    Display *dpy;			/* the display this represents */
    int extension;			/* from XAddExtension */
    struct _CallbackRec *start, *end;	/* linked list of callbacks */
    struct _CallbackRec *calling;	/* currently being called back */
} DisplayEntry;

/*
 * Prototypes
 */
static DisplayEntry *_FindDisplayEntry(Display*, DisplayEntry**);
static Bool _MakeExtension(Display*, int*);

static DisplayEntry *elist = NULL;


/*
 *****************************************************************************
 *			      Public Entry Points                            *
 *****************************************************************************
 */

/*
 * Add - add a callback for the given display.  When the display is closed,
 * the given function will be called as:
 *
 *         (*func) (dpy, arg)
 *
 * This function is declared to return an int even though the value is ignored
 * because some compilers have problems with functions returning void.
 *
 * This routine returns NULL if it was unable to add the callback, otherwise
 * it returns an untyped pointer that can be used with Remove or Lookup, but
 * not dereferenced.
 */
CloseHook
XmuAddCloseDisplayHook(Display *dpy, XmuCloseHookProc func, XPointer arg)
{
    DisplayEntry *de;
    CallbackRec *cb;

    /* allocate ahead of time so that we can fail atomically */
    cb = malloc (sizeof (CallbackRec));
    if (!cb) return ((XPointer) NULL);

    de = _FindDisplayEntry (dpy, NULL);
    if (!de) {
	if ((de = malloc (sizeof (DisplayEntry))) == NULL ||
	    !_MakeExtension (dpy, &de->extension)) {
	    free (cb);
	    free (de);
	    return ((CloseHook) NULL);
	}
	de->dpy = dpy;
	de->start = de->end = NULL;
	de->calling = NULL;
	de->next = elist;
	elist = de;
    }

    /* add to end of list of callback records */
    cb->func = func;
    cb->arg = arg;
    cb->next = NULL;
    if (de->end) {
	de->end->next = cb;
    } else {
	de->start = cb;
    }
    de->end = cb;

    return ((CloseHook) cb);
}


/*
 * Remove - get rid of a callback.  If handle is non-null, use that to compare
 * entries.  Otherwise, remove first instance of the function/argument pair.
 */
Bool
XmuRemoveCloseDisplayHook(Display *dpy, CloseHook handle,
			  XmuCloseHookProc func, XPointer arg)
{
    DisplayEntry *de = _FindDisplayEntry (dpy, NULL);
    register CallbackRec *h, *prev;

    if (!de) return False;

    /* look for handle or function/argument pair */
    for (h = de->start, prev = NULL; h; h = h->next) {
	if (handle) {
	    if (h == (CallbackRec *) handle) break;
	} else {
	    if (h->func == func && h->arg == arg) break;
	}
	prev = h;
    }
    if (!h) return False;


    /* remove from list, watch head and tail */
    if (de->start == h) {
	de->start = h->next;
    } else {
	prev->next = h->next;
    }
    if (de->end == h) de->end = prev;
    if (de->calling != h) free (h);
    return True;
}


/*
 * Lookup - see whether or not a handle has been installed.  If handle is
 * non-NULL, look for an entry that matches it; otherwise look for an entry
 * with the same function/argument pair.
 */
Bool
XmuLookupCloseDisplayHook(Display *dpy, CloseHook handle,
			  XmuCloseHookProc func, XPointer arg)
{
    DisplayEntry *de = _FindDisplayEntry (dpy, NULL);
    register CallbackRec *h;

    if (!de) return False;

    for (h = de->start; h; h = h->next) {
	if (handle) {
	    if (h == (CallbackRec *) handle) break;
	} else {
	    if (h->func == func && h->arg == arg) break;
	}
    }
    return (h ? True : False);
}


/*
 *****************************************************************************
 *			       internal routines                             *
 *****************************************************************************
 */


/*
 * Find the specified display on the linked list of displays.  Also return
 * the preceding link so that the display can be unlinked without having
 * back pointers.
 */
static DisplayEntry *
_FindDisplayEntry(register Display *dpy, DisplayEntry **prevp)
{
    register DisplayEntry *d, *prev;

    for (d = elist, prev = NULL; d; d = d->next) {
	if (d->dpy == dpy) {
	    if (prevp) *prevp = prev;
	    return d;
	}
	prev = d;
    }
    return NULL;
}



/*
 * _DoCallbacks - process all of the callbacks for this display and free
 * the associated callback data (callback records and display entries).
 */
/* ARGSUSED */
static int
_DoCallbacks(Display *dpy, XExtCodes *codes)
{
    register CallbackRec *h;
    DisplayEntry *prev;
    DisplayEntry *de = _FindDisplayEntry (dpy, &prev);

    if (!de) return 0;

    /* walk the list doing the callbacks and freeing callback record */
    for (h = de->start; h;) {
	register CallbackRec *nexth = h->next;
	de->calling = h;		/* let remove know we'll free it */
	(*(h->func)) (dpy, h->arg);
	de->calling = NULL;
	free (h);
	h = nexth;
    }

    /* unlink this display from chain */
    if (elist == de) {
	elist = de->next;
    } else {
	prev->next = de->next;
    }
    free (de);
    return 1;
}


/*
 * _MakeExtension - create an extension for this display; done once per display
 */
static Bool
_MakeExtension(Display *dpy, int *extensionp)
{
    XExtCodes *codes;

    codes = XAddExtension (dpy);
    if (!codes) return False;

    (void) XESetCloseDisplay (dpy, codes->extension, _DoCallbacks);

    *extensionp = codes->extension;
    return True;
}
