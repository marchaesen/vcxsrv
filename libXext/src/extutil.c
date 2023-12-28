/*
 *
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
 *
 * Author:  Jim Fulton, MIT X Consortium
 *
 *
 * 		       Xlib Extension-Writing Utilities
 *
 * This package contains utilities for writing the client API for various
 * protocol extensions.  THESE INTERFACES ARE NOT PART OF THE X STANDARD AND
 * ARE SUBJECT TO CHANGE!
 *
 *  Routines include:
 *
 *         XextCreateExtension		called once per extension
 *         XextDestroyExtension		if no longer using extension
 *         XextAddDisplay		add another display
 *         XextRemoveDisplay		remove a display
 *         XextFindDisplay		is a display open
 *
 * In addition, the following Xlib-style interfaces are provided:
 *
 *         XSetExtensionErrorHandler	establish an extension error handler
 *         XMissingExtension		raise an error about missing ext
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/ge.h>

#include "extutilP.h"

/*
 * XextCreateExtension - return an extension descriptor containing context
 * information for this extension.  This object is passed to all Xext
 * routines.
 */
XExtensionInfo *XextCreateExtension (void)
{
    register XExtensionInfo *info = Xmalloc (sizeof (XExtensionInfo));

    if (info) {
	info->head = NULL;
	info->cur = NULL;
	info->ndisplays = 0;
    }
    return info;
}


/*
 * XextDestroyExtension - free memory the given extension descriptor
 */
void XextDestroyExtension (XExtensionInfo *info)
{
    info->head = NULL;			/* to catch refs after this */
    info->cur = NULL;
    info->ndisplays = 0;
    XFree (info);
}



/*
 * XextAddDisplay - add a display to this extension
 */
XExtDisplayInfo *XextAddDisplay (
    XExtensionInfo *extinfo,
    Display *dpy,
    _Xconst char *ext_name,
    XExtensionHooks *hooks,
    int nevents,
    XPointer data)
{
    XExtDisplayInfo *dpyinfo;

    dpyinfo = Xmalloc (sizeof (XExtDisplayInfo));
    if (!dpyinfo) return NULL;
    dpyinfo->display = dpy;
    dpyinfo->data = data;
    dpyinfo->codes = XInitExtension (dpy, ext_name);

    /*
     * if the server has the extension, then we can initialize the
     * appropriate function vectors
     */
    if (dpyinfo->codes) {
	int i, j;

	for (i = 0, j = dpyinfo->codes->first_event; i < nevents; i++, j++) {
	    XESetWireToEvent (dpy, j, hooks->wire_to_event);
	    XESetEventToWire (dpy, j, hooks->event_to_wire);
	}

        /* register extension for XGE */
        if (strcmp(ext_name, GE_NAME))
            xgeExtRegister(dpy, dpyinfo->codes->major_opcode, hooks);

	if (hooks->create_gc)
	  XESetCreateGC (dpy, dpyinfo->codes->extension, hooks->create_gc);
	if (hooks->copy_gc)
	  XESetCopyGC (dpy, dpyinfo->codes->extension, hooks->copy_gc);
	if (hooks->flush_gc)
	  XESetFlushGC (dpy, dpyinfo->codes->extension, hooks->flush_gc);
	if (hooks->free_gc)
	  XESetFreeGC (dpy, dpyinfo->codes->extension, hooks->free_gc);
	if (hooks->create_font)
	  XESetCreateFont (dpy, dpyinfo->codes->extension, hooks->create_font);
	if (hooks->free_font)
	  XESetFreeFont (dpy, dpyinfo->codes->extension, hooks->free_font);
	if (hooks->close_display)
	  XESetCloseDisplay (dpy, dpyinfo->codes->extension,
			     hooks->close_display);
	if (hooks->error)
	  XESetError (dpy, dpyinfo->codes->extension, hooks->error);
	if (hooks->error_string)
	  XESetErrorString (dpy, dpyinfo->codes->extension,
			    hooks->error_string);
    } else if (hooks->close_display) {
	/* The server doesn't have this extension.
	 * Use a private Xlib-internal extension to hang the close_display
	 * hook on so that the "cache" (extinfo->cur) is properly cleaned.
	 * (XBUG 7955)
	 */
	XExtCodes *codes = XAddExtension(dpy);
	if (!codes) {
	    XFree(dpyinfo);
	    return NULL;
	}
	XESetCloseDisplay (dpy, codes->extension, hooks->close_display);
    }

    /*
     * now, chain it onto the list
     */
    _XLockMutex(_Xglobal_lock);
    dpyinfo->next = extinfo->head;
    extinfo->head = dpyinfo;
    extinfo->cur = dpyinfo;
    extinfo->ndisplays++;
    _XUnlockMutex(_Xglobal_lock);
    return dpyinfo;
}


/*
 * XextRemoveDisplay - remove the indicated display from the extension object
 */
int XextRemoveDisplay (XExtensionInfo *extinfo, Display *dpy)
{
    XExtDisplayInfo *dpyinfo, *prev;

    /*
     * locate this display and its back link so that it can be removed
     */
    _XLockMutex(_Xglobal_lock);
    prev = NULL;
    for (dpyinfo = extinfo->head; dpyinfo; dpyinfo = dpyinfo->next) {
	if (dpyinfo->display == dpy) break;
	prev = dpyinfo;
    }
    if (!dpyinfo) {
	_XUnlockMutex(_Xglobal_lock);
	return 0;		/* hmm, actually an error */
    }

    /*
     * remove the display from the list; handles going to zero
     */
    if (prev)
	prev->next = dpyinfo->next;
    else
	extinfo->head = dpyinfo->next;

    extinfo->ndisplays--;
    if (dpyinfo == extinfo->cur) extinfo->cur = NULL;  /* flush cache */
    _XUnlockMutex(_Xglobal_lock);

    Xfree (dpyinfo);
    return 1;
}


/*
 * XextFindDisplay - look for a display in this extension; keeps a cache
 * of the most-recently used for efficiency.
 */
XExtDisplayInfo *XextFindDisplay (XExtensionInfo *extinfo, Display *dpy)
{
    register XExtDisplayInfo *dpyinfo;

    /*
     * see if this was the most recently accessed display
     */
    if ((dpyinfo = extinfo->cur)&& dpyinfo->display == dpy) return dpyinfo;


    /*
     * look for display in list
     */
    _XLockMutex(_Xglobal_lock);
    for (dpyinfo = extinfo->head; dpyinfo; dpyinfo = dpyinfo->next) {
	if (dpyinfo->display == dpy) {
	    extinfo->cur = dpyinfo;	/* cache most recently used */
	    _XUnlockMutex(_Xglobal_lock);
	    return dpyinfo;
	}
    }
    _XUnlockMutex(_Xglobal_lock);

    return NULL;
}



static int _default_exterror (Display *dpy, _Xconst char *ext_name, _Xconst char *reason)
{
    fprintf (stderr, "Xlib:  extension \"%s\" %s on display \"%s\".\n",
	     ext_name, reason, DisplayString(dpy));
    return 0;
}


/*
 * XSetExtensionErrorHandler - sets the handler that gets called when a
 * requested extension is referenced.  This should eventually move into Xlib.
 */

extern XextErrorHandler _XExtensionErrorFunction;

XextErrorHandler XSetExtensionErrorHandler (XextErrorHandler handler)
{
    XextErrorHandler oldhandler = _XExtensionErrorFunction;

    _XExtensionErrorFunction = (handler ? handler :
				_default_exterror);
    return oldhandler;
}


/*
 * XMissingExtension - call the extension error handler
 */
int XMissingExtension (Display *dpy, _Xconst char *ext_name)
{
    XextErrorHandler func = (_XExtensionErrorFunction ?
			     _XExtensionErrorFunction : _default_exterror);

    if (!ext_name) ext_name = X_EXTENSION_UNKNOWN;
    return (*func) (dpy, ext_name, X_EXTENSION_MISSING);
}
