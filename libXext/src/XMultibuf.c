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
 * Authors:  Jim Fulton, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xlibint.h>
#include <stdio.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/multibufproto.h>
#include <X11/extensions/multibuf.h>

static XExtensionInfo _multibuf_info_data;
static XExtensionInfo *multibuf_info = &_multibuf_info_data;
static const char *multibuf_extension_name = MULTIBUFFER_PROTOCOL_NAME;

#define MbufCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, multibuf_extension_name, val)
#define MbufSimpleCheckExtension(dpy,i) \
  XextSimpleCheckExtension (dpy, i, multibuf_extension_name)


/*****************************************************************************
 *                                                                           *
 *			   private utility routines                          *
 *                                                                           *
 *****************************************************************************/

/*
 * find_display - locate the display info block
 */
static int close_display(Display *dpy, XExtCodes *codes);
static char *error_string(Display *dpy, int code, XExtCodes *codes, char *buf, int n);
static Bool wire_to_event(Display *dpy, XEvent *libevent, xEvent *netevent);
static Status event_to_wire(Display *dpy, XEvent *libevent, xEvent *netevent);
static /* const */ XExtensionHooks multibuf_extension_hooks = {
    NULL,				/* create_gc */
    NULL,				/* copy_gc */
    NULL,				/* flush_gc */
    NULL,				/* free_gc */
    NULL,				/* create_font */
    NULL,				/* free_font */
    close_display,			/* close_display */
    wire_to_event,			/* wire_to_event */
    event_to_wire,			/* event_to_wire */
    NULL,				/* error */
    error_string,			/* error_string */
};

static const char *multibuf_error_list[] = {
    "BadBuffer",			/* MultibufferBadBuffer */
};

static XEXT_GENERATE_FIND_DISPLAY (find_display, multibuf_info,
				   multibuf_extension_name,
				   &multibuf_extension_hooks,
				   MultibufferNumberEvents, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, multibuf_info)

static XEXT_GENERATE_ERROR_STRING (error_string, multibuf_extension_name,
				   MultibufferNumberErrors,
				   multibuf_error_list)

/*
 * wire_to_event - convert a wire event in network format to a C
 * event structure
 */
static Bool wire_to_event (Display *dpy, XEvent *libevent, xEvent *netevent)
{
    XExtDisplayInfo *info = find_display (dpy);

    MbufCheckExtension (dpy, info, False);

    switch ((netevent->u.u.type & 0x7f) - info->codes->first_event) {
      case MultibufferClobberNotify:
	{
	    XmbufClobberNotifyEvent *ev;
	    xMbufClobberNotifyEvent *event;

    	    ev = (XmbufClobberNotifyEvent *) libevent;
	    event = (xMbufClobberNotifyEvent *) netevent;
    	    ev->type = event->type & 0x7f;
    	    ev->serial = _XSetLastRequestRead(dpy,(xGenericReply *) netevent);
    	    ev->send_event = ((event->type & 0x80) != 0);
    	    ev->display = dpy;
    	    ev->buffer = event->buffer;
	    ev->state = event->state;
    	    return True;
	}
      case MultibufferUpdateNotify:
	{
	    XmbufUpdateNotifyEvent *ev;
	    xMbufUpdateNotifyEvent *event;

	    ev = (XmbufUpdateNotifyEvent *) libevent;
	    event = (xMbufUpdateNotifyEvent *) netevent;
	    ev->type = event->type & 0x7f;
	    ev->serial = _XSetLastRequestRead(dpy,(xGenericReply *) netevent);
	    ev->send_event = ((event->type & 0x80) != 0);
	    ev->display = dpy;
	    ev->buffer = event->buffer;
	    return True;
	}
    }
    return False;
}


/*
 * event_to_wire - convert a C event structure to a wire event in
 * network format
 */
static Status event_to_wire (Display *dpy, XEvent *libevent, xEvent *netevent)
{
    XExtDisplayInfo *info = find_display (dpy);

    MbufCheckExtension (dpy, info, 0);

    switch ((libevent->type & 0x7f) - info->codes->first_event) {
      case MultibufferClobberNotify:
	{
	    XmbufClobberNotifyEvent *ev;
	    xMbufClobberNotifyEvent *event;

    	    ev = (XmbufClobberNotifyEvent *) libevent;
	    event = (xMbufClobberNotifyEvent *) netevent;
    	    event->type = ev->type;
    	    event->sequenceNumber = (ev->serial & 0xffff);
    	    event->buffer = ev->buffer;
	    event->state = ev->state;
    	    return 1;
	}
      case MultibufferUpdateNotify:
	{
	    XmbufUpdateNotifyEvent *ev;
	    xMbufUpdateNotifyEvent *event;

	    ev = (XmbufUpdateNotifyEvent *) libevent;
	    event = (xMbufUpdateNotifyEvent *) netevent;
	    event->type = ev->type;
	    event->sequenceNumber = (ev->serial & 0xffff);
	    event->buffer = ev->buffer;
	    return 1;
	}
    }
    return 0;
}


/*
 * read_buffer_info - read Buffer Info descriptors from the net; if unable
 * to allocate memory, read junk to make sure that stream is clear.
 */
#define TALLOC(type,count) ((type *) Xmalloc ((unsigned) count * sizeof(type)))

static XmbufBufferInfo *read_buffer_info (Display *dpy, int nbufs)
{
    xMbufBufferInfo *netbuf = TALLOC (xMbufBufferInfo, nbufs);
    XmbufBufferInfo *bufinfo = NULL;
    long netbytes = nbufs * SIZEOF(xMbufBufferInfo);

    if (netbuf) {
	_XRead (dpy, (char *) netbuf, netbytes);

	bufinfo = TALLOC (XmbufBufferInfo, nbufs);
	if (bufinfo) {
	    register XmbufBufferInfo *c;
	    register xMbufBufferInfo *net;
	    register int i;

	    for (i = 0, c = bufinfo, net = netbuf; i < nbufs;
		 i++, c++, net++) {
		c->visualid = net->visualID;
		c->max_buffers = net->maxBuffers;
		c->depth = net->depth;
	    }
	}
	Xfree ((char *) netbuf);
    } else {				/* eat the data */
	while (netbytes > 0) {
	    char dummy[256];		/* stack size vs loops tradeoff */
	    long nbytes = sizeof dummy;

	    if (nbytes > netbytes) nbytes = netbytes;
	    _XRead (dpy, dummy, nbytes);
	    netbytes -= nbytes;
	}
    }

    return bufinfo;
}

#undef TALLOC


/*****************************************************************************
 *                                                                           *
 *		    Multibuffering/stereo public interfaces                  *
 *                                                                           *
 *****************************************************************************/


/*
 * XmbufQueryExtension -
 * 	Returns True if the multibuffering/stereo extension is available
 * 	on the given display.  If the extension exists, the value of the
 * 	first event code (which should be added to the event type constants
 * 	MultibufferClobberNotify and MultibufferUpdateNotify to get the
 * 	actual values) is stored into event_base and the value of the first
 * 	error code (which should be added to the error type constant
 * 	MultibufferBadBuffer to get the actual value) is stored into
 * 	error_base.
 */
Bool XmbufQueryExtension (
    Display *dpy,
    int *event_base_return, int *error_base_return)
{
    XExtDisplayInfo *info = find_display (dpy);

    if (XextHasExtension (info)) {
	*event_base_return = info->codes->first_event;
	*error_base_return = info->codes->first_error;
	return True;
    } else {
	return False;
    }
}


/*
 * XmbufGetVersion -
 * 	Gets the major and minor version numbers of the extension.  The return
 * 	value is zero if an error occurs or non-zero if no error happens.
 */
Status XmbufGetVersion (
    Display *dpy,
    int *major_version_return, int *minor_version_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    xMbufGetBufferVersionReply rep;
    register xMbufGetBufferVersionReq *req;

    MbufCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    MbufGetReq (MbufGetBufferVersion, req, info);
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    *major_version_return = rep.majorVersion;
    *minor_version_return = rep.minorVersion;
    UnlockDisplay (dpy);

    SyncHandle ();
    return 1;
}


/*
 * XmbufCreateBuffers -
 * 	Requests that "count" buffers be created with the given update_action
 * 	and update_hint and be associated with the indicated window.  The
 * 	number of buffers created is returned (zero if an error occurred)
 * 	and buffers_return is filled in with that many Multibuffer identifiers.
 */
int XmbufCreateBuffers (
    Display *dpy,
    Window w,
    int count,
    int update_action, int update_hint,
    Multibuffer *buffers)
{
    XExtDisplayInfo *info = find_display (dpy);
    xMbufCreateImageBuffersReply rep;
    register xMbufCreateImageBuffersReq *req;
    int result;

    MbufCheckExtension (dpy, info, 0);

    LockDisplay (dpy);

    XAllocIDs(dpy, buffers, count);
    MbufGetReq (MbufCreateImageBuffers, req, info);
    req->window = w;
    req->updateAction = update_action;
    req->updateHint = update_hint;
    req->length += count;
    count <<= 2;
    PackData32 (dpy, buffers, count);
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    result = rep.numberBuffer;
    UnlockDisplay (dpy);

    SyncHandle ();
    return result;
}


/*
 * XmbufDestroyBuffers -
 * 	Destroys the buffers associated with the given window.
 */
void XmbufDestroyBuffers (Display *dpy, Window window)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufDestroyImageBuffersReq *req;

    MbufSimpleCheckExtension (dpy, info);

    LockDisplay (dpy);
    MbufGetReq (MbufDestroyImageBuffers, req, info);
    req->window = window;
    UnlockDisplay (dpy);
    SyncHandle ();
}


/*
 * XmbufDisplayBuffers -
 * 	Displays the indicated buffers their appropriate windows within
 * 	max_delay milliseconds after min_delay milliseconds have passed.
 * 	No two buffers may be associated with the same window or else a Match
 * 	error is generated.
 */
void XmbufDisplayBuffers (
    Display *dpy,
    int count,
    Multibuffer *buffers,
    int min_delay, int max_delay)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufDisplayImageBuffersReq *req;

    MbufSimpleCheckExtension (dpy, info);

    LockDisplay (dpy);
    MbufGetReq (MbufDisplayImageBuffers, req, info);
    req->minDelay = min_delay;
    req->maxDelay = max_delay;
    req->length += count;
    count <<= 2;
    PackData32 (dpy, buffers, count);
    UnlockDisplay (dpy);
    SyncHandle();
}


/*
 * XmbufGetWindowAttributes -
 * 	Gets the multibuffering attributes that apply to all buffers associated
 * 	with the given window.  Returns non-zero on success and zero if an
 * 	error occurs.
 */
Status XmbufGetWindowAttributes (
    Display *dpy,
    Window w,
    XmbufWindowAttributes *attr)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufGetMBufferAttributesReq *req;
    xMbufGetMBufferAttributesReply rep;

    MbufCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    MbufGetReq (MbufGetMBufferAttributes, req, info);
    req->window = w;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    attr->buffers = (Multibuffer *) NULL;
    if ((attr->nbuffers = rep.length)) {
	int nbytes = rep.length * sizeof(Multibuffer);
	attr->buffers = (Multibuffer *) Xmalloc((unsigned) nbytes);
	nbytes = rep.length << 2;
	if (! attr->buffers) {
	    _XEatDataWords(dpy, rep.length);
	    UnlockDisplay(dpy);
	    SyncHandle();
	    return (0);
	}
	_XRead32 (dpy, (long *) attr->buffers, nbytes);
    }
    attr->displayed_index = rep.displayedBuffer;
    attr->update_action = rep.updateAction;
    attr->update_hint = rep.updateHint;
    attr->window_mode = rep.windowMode;

    UnlockDisplay (dpy);
    SyncHandle();
    return 1;
}


/*
 * XmbufChangeWindowAttributes -
 * 	Sets the multibuffering attributes that apply to all buffers associated
 * 	with the given window.  This is currently limited to the update_hint.
 */
void XmbufChangeWindowAttributes (
    Display *dpy,
    Window w,
    unsigned long valuemask,
    XmbufSetWindowAttributes *attr)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufSetMBufferAttributesReq *req;

    MbufSimpleCheckExtension (dpy, info);

    LockDisplay (dpy);
    MbufGetReq (MbufSetMBufferAttributes, req, info);
    req->window = w;
    if ((req->valueMask = valuemask)) {	/* stolen from lib/X/XWindow.c */
	unsigned long values[1];	/* one per element in if stmts below */
	unsigned long *v = values;
	unsigned int nvalues;

	if (valuemask & MultibufferWindowUpdateHint)
	  *v++ = attr->update_hint;
	req->length += (nvalues = v - values);
	nvalues <<= 2;			/* watch out for macros... */
	Data32 (dpy, (long *) values, (long)nvalues);
    }
    UnlockDisplay (dpy);
    SyncHandle();
}


/*
 * XmbufGetBufferAttributes -
 * 	Gets the attributes for the indicated buffer.  Returns non-zero on
 * 	success and zero if an error occurs.
 */
Status XmbufGetBufferAttributes (
    Display *dpy,
    Multibuffer b,
    XmbufBufferAttributes *attr)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufGetBufferAttributesReq *req;
    xMbufGetBufferAttributesReply rep;

    MbufCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    MbufGetReq (MbufGetBufferAttributes, req, info);
    req->buffer = b;
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    attr->window = rep.window;
    attr->event_mask = rep.eventMask;
    attr->buffer_index = rep.bufferIndex;
    attr->side = rep.side;

    UnlockDisplay (dpy);
    SyncHandle();
    return 1;
}


/*
 * XmbufChangeBufferAttributes -
 * 	Sets the attributes for the indicated buffer.  This is currently
 * 	limited to the event_mask.
 */
void XmbufChangeBufferAttributes (
    Display *dpy,
    Multibuffer b,
    unsigned long valuemask,
    XmbufSetBufferAttributes *attr)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufSetBufferAttributesReq *req;

    MbufSimpleCheckExtension (dpy, info);

    LockDisplay (dpy);
    MbufGetReq (MbufSetBufferAttributes, req, info);
    req->buffer = b;
    if ((req->valueMask = valuemask)) {	/* stolen from lib/X/XWindow.c */
	unsigned long values[1];	/* one per element in if stmts below */
	unsigned long *v = values;
	unsigned int nvalues;

	if (valuemask & MultibufferBufferEventMask)
	  *v++ = attr->event_mask;
	req->length += (nvalues = v - values);
	nvalues <<= 2;			/* watch out for macros... */
	Data32 (dpy, (long *) values, (long)nvalues);
    }
    UnlockDisplay (dpy);
    SyncHandle();
}



/*
 * XmbufGetScreenInfo -
 * 	Gets the parameters controlling how mono and stereo windows may be
 * 	created on the indicated screen.  The numbers of sets of visual and
 * 	depths are returned in nmono_return and nstereo_return.  If
 * 	nmono_return is greater than zero, then mono_info_return is set to
 * 	the address of an array of XmbufBufferInfo structures describing the
 * 	various visuals and depths that may be used.  Otherwise,
 * 	mono_info_return is set to NULL.  Similarly, stereo_info_return is
 * 	set according to nstereo_return.  The storage returned in
 * 	mono_info_return and stereo_info_return may be released by XFree.
 * 	If no errors are encounted, non-zero will be returned.
 */
Status XmbufGetScreenInfo (
    Display *dpy,
    Drawable d,
    int *nmono_return,
    XmbufBufferInfo **mono_info_return,
    int *nstereo_return,
    XmbufBufferInfo **stereo_info_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufGetBufferInfoReq *req;
    xMbufGetBufferInfoReply rep;
    int nmono, nstereo;
    XmbufBufferInfo *minfo, *sinfo;

    MbufCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    MbufGetReq (MbufGetBufferInfo, req, info);
    req->drawable = d;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    nmono = rep.normalInfo;
    nstereo = rep.stereoInfo;
    minfo = ((nmono > 0) ? read_buffer_info (dpy, nmono) : NULL);
    sinfo = ((nstereo > 0) ? read_buffer_info (dpy, nstereo) : NULL);

    /* check for bad reads indicating we need to return an error */
    if ((nmono > 0 && !minfo) || (nstereo > 0 && !sinfo)) {
	if (minfo) Xfree ((char *) minfo);
	if (sinfo) Xfree ((char *) sinfo);
	UnlockDisplay (dpy);
	SyncHandle();
	return 0;
    }

    *nmono_return = nmono;
    *mono_info_return = minfo;
    *nstereo_return = nstereo;
    *stereo_info_return = sinfo;

    UnlockDisplay (dpy);
    SyncHandle();
    return 1;
}


/*
 * XmbufCreateStereoWindow -
 * 	Creates a stereo window in the same way that XCreateWindow creates
 * 	a mono window (in fact, use the same code, except for the request)
 *      and returns the left and right buffers that may be
 */
Window XmbufCreateStereoWindow (
    Display *dpy,
    Window parent,
    int x, int y,
    unsigned int width, unsigned int height, unsigned int border_width,
    int depth,
    unsigned int class,
    Visual *visual,
    unsigned long valuemask,
    XSetWindowAttributes *attr,
    Multibuffer *leftp, Multibuffer *rightp)
{
    XExtDisplayInfo *info = find_display (dpy);
    Window wid;
    register xMbufCreateStereoWindowReq *req;

    MbufCheckExtension (dpy, info, None);

    LockDisplay(dpy);
    MbufGetReq(MbufCreateStereoWindow, req, info);
    wid = req->wid = XAllocID(dpy);
    req->parent = parent;
    req->left = *leftp = XAllocID (dpy);
    req->right = *rightp = XAllocID (dpy);
    req->x = x;
    req->y = y;
    req->width = width;
    req->height = height;
    req->borderWidth = border_width;
    req->depth = depth;
    req->class = class;
    if (visual == (Visual *)CopyFromParent)
	req->visual = CopyFromParent;
    else
	req->visual = visual->visualid;
    valuemask &= (CWBackPixmap|CWBackPixel|CWBorderPixmap|
		     CWBorderPixel|CWBitGravity|CWWinGravity|
		     CWBackingStore|CWBackingPlanes|CWBackingPixel|
		     CWOverrideRedirect|CWSaveUnder|CWEventMask|
		     CWDontPropagate|CWColormap|CWCursor);
    if ((req->mask = valuemask)) {
	unsigned long values[32];
	register unsigned long *value = values;
	unsigned int nvalues;

	if (valuemask & CWBackPixmap)
	  *value++ = attr->background_pixmap;
	if (valuemask & CWBackPixel)
	  *value++ = attr->background_pixel;
	if (valuemask & CWBorderPixmap)
	  *value++ = attr->border_pixmap;
	if (valuemask & CWBorderPixel)
	  *value++ = attr->border_pixel;
	if (valuemask & CWBitGravity)
	  *value++ = attr->bit_gravity;
	if (valuemask & CWWinGravity)
	  *value++ = attr->win_gravity;
	if (valuemask & CWBackingStore)
	  *value++ = attr->backing_store;
	if (valuemask & CWBackingPlanes)
	  *value++ = attr->backing_planes;
	if (valuemask & CWBackingPixel)
	  *value++ = attr->backing_pixel;
	if (valuemask & CWOverrideRedirect)
	  *value++ = attr->override_redirect;
	if (valuemask & CWSaveUnder)
	  *value++ = attr->save_under;
	if (valuemask & CWEventMask)
	  *value++ = attr->event_mask;
	if (valuemask & CWDontPropagate)
	  *value++ = attr->do_not_propagate_mask;
	if (valuemask & CWColormap)
	  *value++ = attr->colormap;
	if (valuemask & CWCursor)
	  *value++ = attr->cursor;
	req->length += (nvalues = value - values);

	nvalues <<= 2;			    /* watch out for macros... */
	Data32 (dpy, (long *) values, (long)nvalues);
    }
    UnlockDisplay(dpy);
    SyncHandle();
    return wid;
}

void XmbufClearBufferArea (
    Display *dpy,
    Multibuffer buffer,
    int x, int y,
    unsigned int width, unsigned int height,
    Bool exposures)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xMbufClearImageBufferAreaReq *req;

    MbufSimpleCheckExtension (dpy, info);

    LockDisplay (dpy);
    MbufGetReq (MbufClearImageBufferArea, req, info);
    req->buffer = buffer;
    req->x      = x;
    req->y      = y;
    req->width  = width;
    req->height = height;
    req->exposures = exposures;
    UnlockDisplay (dpy);
    SyncHandle();
}

