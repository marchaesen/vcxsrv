/*
 *
 * Copyright © 2000 SuSE, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of SuSE not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  SuSE makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * SuSE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL SuSE
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Keith Packard, SuSE, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xrenderint.h"
#include <limits.h>

XRenderExtInfo XRenderExtensionInfo;
char XRenderExtensionName[] = RENDER_NAME;

static int XRenderCloseDisplay (Display *dpy, XExtCodes *codes);

/*
 * XRenderExtFindDisplay - look for a display in this extension; keeps a
 * cache of the most-recently used for efficiency. (Replaces
 * XextFindDisplay.)
 */
static XRenderExtDisplayInfo *
XRenderExtFindDisplay (XRenderExtInfo *extinfo,
                       Display        *dpy)
{
    XRenderExtDisplayInfo *dpyinfo;

    /*
     * see if this was the most recently accessed display
     */
    if ((dpyinfo = extinfo->cur) && dpyinfo->display == dpy)
        return dpyinfo;

    /*
     * look for display in list
     */
    _XLockMutex(_Xglobal_lock);
    for (dpyinfo = extinfo->head; dpyinfo; dpyinfo = dpyinfo->next) {
        if (dpyinfo->display == dpy) {
            extinfo->cur = dpyinfo;     /* cache most recently used */
            _XUnlockMutex(_Xglobal_lock);
            return dpyinfo;
        }
    }
    _XUnlockMutex(_Xglobal_lock);

    return NULL;
}

/*
 * If the server is missing support for any of the required depths on
 * any screen, tell the application that Render is not present.
 */

#define DEPTH_MASK(d)	(1U << ((d) - 1))

/*
 * Render requires support for depth 1, 4, 8, 24 and 32 pixmaps
 */

#define REQUIRED_DEPTHS	(DEPTH_MASK(1) | \
			 DEPTH_MASK(4) | \
			 DEPTH_MASK(8) | \
			 DEPTH_MASK(24) | \
			 DEPTH_MASK(32))

typedef struct _DepthCheckRec {
    struct _DepthCheckRec *next;
    Display *dpy;
    CARD32  missing;
    unsigned long serial;
} DepthCheckRec, *DepthCheckPtr;

static DepthCheckPtr	depthChecks;

static int
XRenderDepthCheckErrorHandler (Display *dpy, XErrorEvent *evt)
{
    if (evt->request_code == X_CreatePixmap && evt->error_code == BadValue)
    {
	DepthCheckPtr	d;

	_XLockMutex(_Xglobal_lock);
	for (d = depthChecks; d; d = d->next)
	{
	    if (d->dpy == dpy)
	    {
		if ((long) (evt->serial - d->serial) >= 0)
		    d->missing |= DEPTH_MASK(evt->resourceid);
		break;
	    }
        }
	_XUnlockMutex (_Xglobal_lock);
    }
    return 0;
}

static Bool
XRenderHasDepths (Display *dpy)
{
    int	s;

    for (s = 0; s < ScreenCount (dpy); s++)
    {
	CARD32		    depths = 0;
	CARD32		    missing;
	Screen		    *scr = ScreenOfDisplay (dpy, s);
	int		    d;

	for (d = 0; d < scr->ndepths; d++)
	    depths |= DEPTH_MASK(scr->depths[d].depth);
	missing = ~depths & REQUIRED_DEPTHS;
	if (missing)
	{
	    DepthCheckRec   dc, **dp;
	    XErrorHandler   previousHandler;

	    /*
	     * Ok, this is ugly.  It should be sufficient at this
	     * point to just return False, but Xinerama is broken at
	     * this point and only advertises depths which have an
	     * associated visual.  Of course, the other depths still
	     * work, but the only way to find out is to try them.
	     */
	    dc.dpy = dpy;
	    dc.missing = 0;
	    dc.serial = XNextRequest (dpy);
	    _XLockMutex(_Xglobal_lock);
	    dc.next = depthChecks;
	    depthChecks = &dc;
	    _XUnlockMutex (_Xglobal_lock);
	    /*
	     * I suspect this is not really thread safe, but Xlib doesn't
	     * provide a lot of options here
	     */
	    previousHandler = XSetErrorHandler (XRenderDepthCheckErrorHandler);
	    /*
	     * Try each missing depth and see if pixmap creation succeeds
	     */
	    for (d = 1; d <= 32; d++)
		/* don't check depth 1 == Xcursor recurses... */
		if ((missing & DEPTH_MASK(d)) && d != 1)
		{
		    Pixmap  p;
		    p = XCreatePixmap (dpy, RootWindow (dpy, s), 1, 1, (unsigned) d);
		    XFreePixmap (dpy, p);
		}
	    XSync (dpy, False);
	    XSetErrorHandler (previousHandler);
	    /*
	     * Unhook from the list of depth check records
	     */
	    _XLockMutex(_Xglobal_lock);
	    for (dp = &depthChecks; *dp; dp = &(*dp)->next)
	    {
		if (*dp == &dc)
		{
		    *dp = dc.next;
		    break;
		}
	    }
	    _XUnlockMutex (_Xglobal_lock);
	    if (dc.missing)
		return False;
	}
    }
    return True;
}

/*
 * XRenderExtAddDisplay - add a display to this extension. (Replaces
 * XextAddDisplay)
 */
static XRenderExtDisplayInfo *
XRenderExtAddDisplay (XRenderExtInfo *extinfo,
                      Display        *dpy,
                      char           *ext_name)
{
    XRenderExtDisplayInfo *dpyinfo;

    dpyinfo = Xmalloc (sizeof (XRenderExtDisplayInfo));
    if (!dpyinfo) return NULL;
    dpyinfo->display = dpy;
    dpyinfo->info = NULL;

    if (XRenderHasDepths (dpy))
	dpyinfo->codes = XInitExtension (dpy, ext_name);
    else
	dpyinfo->codes = NULL;

    /*
     * if the server has the extension, then we can initialize the
     * appropriate function vectors
     */
    if (dpyinfo->codes) {
        XESetCloseDisplay (dpy, dpyinfo->codes->extension,
                           XRenderCloseDisplay);
    } else {
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
        XESetCloseDisplay (dpy, codes->extension, XRenderCloseDisplay);
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
 * XRenderExtRemoveDisplay - remove the indicated display from the
 * extension object. (Replaces XextRemoveDisplay.)
 */
static int
XRenderExtRemoveDisplay (XRenderExtInfo *extinfo, Display *dpy)
{
    XRenderExtDisplayInfo *dpyinfo, *prev;

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



XRenderExtDisplayInfo *
XRenderFindDisplay (Display *dpy)
{
    XRenderExtDisplayInfo *dpyinfo;

    dpyinfo = XRenderExtFindDisplay (&XRenderExtensionInfo, dpy);
    if (!dpyinfo)
	dpyinfo = XRenderExtAddDisplay (&XRenderExtensionInfo, dpy,
                                        XRenderExtensionName);
    return dpyinfo;
}

static int
XRenderCloseDisplay (Display *dpy, XExtCodes *codes _X_UNUSED)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    if (info && info->info) XFree (info->info);

    return XRenderExtRemoveDisplay (&XRenderExtensionInfo, dpy);
}

/****************************************************************************
 *                                                                          *
 *			    Render public interfaces                        *
 *                                                                          *
 ****************************************************************************/

Bool XRenderQueryExtension (Display *dpy, int *event_basep, int *error_basep)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);

    if (RenderHasExtension(info)) {
	*event_basep = info->codes->first_event;
	*error_basep = info->codes->first_error;
	return True;
    } else {
	return False;
    }
}


Status XRenderQueryVersion (Display *dpy,
			    int	    *major_versionp,
			    int	    *minor_versionp)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    XRenderInfo	    *xri;

    if (!RenderHasExtension (info))
	return 0;

    if (!XRenderQueryFormats (dpy))
	return 0;

    xri = info->info;
    *major_versionp = xri->major_version;
    *minor_versionp = xri->minor_version;
    return 1;
}

static XRenderPictFormat *
_XRenderFindFormat (XRenderInfo *xri, PictFormat format)
{
    int	nf;

    for (nf = 0; nf < xri->nformat; nf++)
	if (xri->format[nf].id == format)
	    return &xri->format[nf];
    return NULL;
}

static Visual *
_XRenderFindVisual (Display *dpy, VisualID vid)
{
    return _XVIDtoVisual (dpy, vid);
}

typedef struct _renderVersionState {
    unsigned long   version_seq;
    Bool	    error;
    int		    major_version;
    int		    minor_version;

} _XrenderVersionState;

static Bool
_XRenderVersionHandler (Display	    *dpy,
			xReply	    *rep,
			char	    *buf,
			int	    len,
			XPointer    data)
{
    xRenderQueryVersionReply	replbuf;
    xRenderQueryVersionReply	*repl;
    _XrenderVersionState	*state = (_XrenderVersionState *) data;

    if (dpy->last_request_read != state->version_seq)
	return False;
    if (rep->generic.type == X_Error)
    {
	state->error = True;
	return False;
    }
    repl = (xRenderQueryVersionReply *)
	_XGetAsyncReply(dpy, (char *)&replbuf, rep, buf, len,
		     (SIZEOF(xRenderQueryVersionReply) - SIZEOF(xReply)) >> 2,
			True);
    state->major_version = (int) repl->majorVersion;
    state->minor_version = (int) repl->minorVersion;
    return True;
}

Status
XRenderQueryFormats (Display *dpy)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    _XAsyncHandler		async;
    _XrenderVersionState	async_state;
    xRenderQueryVersionReq	*vreq;
    xRenderQueryPictFormatsReply rep;
    xRenderQueryPictFormatsReq  *req;
    XRenderInfo			*xri;
    XRenderPictFormat		*format;
    XRenderScreen		*screen;
    XRenderDepth		*depth;
    XRenderVisual		*visual;
    xPictFormInfo		*xFormat;
    xPictScreen			*xScreen;
    xPictDepth			*xPDepth;
    xPictVisual			*xVisual;
    CARD32			*xSubpixel;
    void			*xData;
    int				ns, nd;
    unsigned			nf;
    unsigned long		rlength;
    unsigned long		nbytes;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay (dpy);
    if (info->info)
    {
	UnlockDisplay (dpy);
	return 1;
    }
    GetReq (RenderQueryVersion, vreq);
    vreq->reqType = (CARD8) info->codes->major_opcode;
    vreq->renderReqType = X_RenderQueryVersion;
    vreq->majorVersion = RENDER_MAJOR;
    vreq->minorVersion = RENDER_MINOR;

    async_state.version_seq = dpy->request;
    async_state.error = False;
    async.next = dpy->async_handlers;
    async.handler = _XRenderVersionHandler;
    async.data = (XPointer) &async_state;
    dpy->async_handlers = &async;

    GetReq (RenderQueryPictFormats, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderQueryPictFormats;

    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse))
    {
	DeqAsyncHandler (dpy, &async);
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    DeqAsyncHandler (dpy, &async);
    if (async_state.error)
    {
	UnlockDisplay(dpy);
	SyncHandle();
	return 0;
    }
    /*
     * Check for the lack of sub-pixel data
     */
    if (async_state.major_version == 0 && async_state.minor_version < 6)
	rep.numSubpixel = 0;

    if ((rep.numFormats < ((INT_MAX / 4) / sizeof (XRenderPictFormat))) &&
	(rep.numScreens < ((INT_MAX / 4) / sizeof (XRenderScreen))) &&
	(rep.numDepths  < ((INT_MAX / 4) / sizeof (XRenderDepth))) &&
	(rep.numVisuals < ((INT_MAX / 4) / sizeof (XRenderVisual))) &&
	(rep.numSubpixel < ((INT_MAX / 4) / 4)) &&
	(rep.length < (INT_MAX >> 2)) ) {
	xri = Xmalloc (sizeof (XRenderInfo) +
		       (rep.numFormats * sizeof (XRenderPictFormat)) +
		       (rep.numScreens * sizeof (XRenderScreen)) +
		       (rep.numDepths * sizeof (XRenderDepth)) +
		       (rep.numVisuals * sizeof (XRenderVisual)));
	rlength = ((rep.numFormats * sizeof (xPictFormInfo)) +
		   (rep.numScreens * sizeof (xPictScreen)) +
		   (rep.numDepths * sizeof (xPictDepth)) +
		   (rep.numVisuals * sizeof (xPictVisual)) +
		   (rep.numSubpixel * 4));
	xData = Xmalloc (rlength);
	nbytes = (unsigned long) rep.length << 2;
    } else {
	xri = NULL;
	xData = NULL;
	rlength = nbytes = 0;
    }

    if (!xri || !xData || nbytes < rlength)
    {
	if (xri) Xfree (xri);
	if (xData) Xfree (xData);
	_XEatDataWords (dpy, rep.length);
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    xri->major_version = async_state.major_version;
    xri->minor_version = async_state.minor_version;
    xri->format = (XRenderPictFormat *) (xri + 1);
    xri->nformat = (int) rep.numFormats;
    xri->screen = (XRenderScreen *) (xri->format + rep.numFormats);
    xri->nscreen = (int) rep.numScreens;
    xri->depth = (XRenderDepth *) (xri->screen + rep.numScreens);
    xri->ndepth = (int) rep.numDepths;
    xri->visual = (XRenderVisual *) (xri->depth + rep.numDepths);
    xri->nvisual = (int) rep.numVisuals;
    _XRead (dpy, (char *) xData, (long) rlength);
    format = xri->format;
    xFormat = (xPictFormInfo *) xData;
    for (nf = 0; nf < rep.numFormats; nf++)
    {
	format->id = xFormat->id;
	format->type = xFormat->type;
	format->depth = xFormat->depth;
	format->direct.red       = (short) xFormat->direct.red;
	format->direct.redMask   = (short) xFormat->direct.redMask;
	format->direct.green     = (short) xFormat->direct.green;
	format->direct.greenMask = (short) xFormat->direct.greenMask;
	format->direct.blue      = (short) xFormat->direct.blue;
	format->direct.blueMask  = (short) xFormat->direct.blueMask;
	format->direct.alpha     = (short) xFormat->direct.alpha;
	format->direct.alphaMask = (short) xFormat->direct.alphaMask;
	format->colormap = xFormat->colormap;
	format++;
	xFormat++;
    }
    xScreen = (xPictScreen *) xFormat;
    screen = xri->screen;
    depth = xri->depth;
    visual = xri->visual;
    for (ns = 0; ns < xri->nscreen; ns++)
    {
	screen->depths = depth;
	screen->ndepths = (int) xScreen->nDepth;
	screen->fallback = _XRenderFindFormat (xri, xScreen->fallback);
	screen->subpixel = SubPixelUnknown;
	xPDepth = (xPictDepth *) (xScreen + 1);
	if (screen->ndepths > rep.numDepths) {
	    Xfree (xri);
	    Xfree (xData);
	    _XEatDataWords (dpy, rep.length);
	    UnlockDisplay (dpy);
	    SyncHandle ();
	    return 0;
	}
	rep.numDepths -= (CARD32) screen->ndepths;
	for (nd = 0; nd < screen->ndepths; nd++)
	{
	    int nv;

	    depth->depth = xPDepth->depth;
	    depth->nvisuals = xPDepth->nPictVisuals;
	    depth->visuals = visual;
	    xVisual = (xPictVisual *) (xPDepth + 1);
	    if (depth->nvisuals > rep.numVisuals) {
		Xfree (xri);
		Xfree (xData);
		_XEatDataWords (dpy, rep.length);
		UnlockDisplay (dpy);
		SyncHandle ();
		return 0;
	    }
	    rep.numVisuals -= (CARD32) depth->nvisuals;
	    for (nv = 0; nv < depth->nvisuals; nv++)
	    {
		visual->visual = _XRenderFindVisual (dpy, xVisual->visual);
		visual->format = _XRenderFindFormat (xri, xVisual->format);
		visual++;
		xVisual++;
	    }
	    depth++;
	    xPDepth = (xPictDepth *) xVisual;
	}
	screen++;
	xScreen = (xPictScreen *) xPDepth;
    }
    xSubpixel = (CARD32 *) xScreen;
    screen = xri->screen;
    for (ns = 0; ns < rep.numSubpixel; ns++)
    {
	screen->subpixel = (int) *xSubpixel;
	xSubpixel++;
	screen++;
    }
    info->info = xri;
    /*
     * Skip any extra data
     */
    if (nbytes > rlength)
	_XEatData (dpy, (unsigned long) (nbytes - rlength));

    UnlockDisplay (dpy);
    SyncHandle ();
    Xfree (xData);
    return 1;
}

int
XRenderQuerySubpixelOrder (Display *dpy, int screen)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    XRenderInfo	    *xri;

    if (!RenderHasExtension (info))
	return SubPixelUnknown;

    if (!XRenderQueryFormats (dpy))
	return SubPixelUnknown;

    xri = info->info;
    return xri->screen[screen].subpixel;
}

Bool
XRenderSetSubpixelOrder (Display *dpy, int screen, int subpixel)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    XRenderInfo	    *xri;

    if (!RenderHasExtension (info))
	return False;

    if (!XRenderQueryFormats (dpy))
	return False;

    xri = info->info;
    xri->screen[screen].subpixel = subpixel;
    return True;
}

XRenderPictFormat *
XRenderFindVisualFormat (Display *dpy, _Xconst Visual *visual)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    int		    nv;
    XRenderInfo	    *xri;
    XRenderVisual   *xrv;

    RenderCheckExtension (dpy, info, NULL);
    if (!XRenderQueryFormats (dpy))
        return NULL;
    xri = info->info;
    for (nv = 0, xrv = xri->visual; nv < xri->nvisual; nv++, xrv++)
	if (xrv->visual == visual)
	    return xrv->format;
    return NULL;
}

XRenderPictFormat *
XRenderFindFormat (Display		*dpy,
		   unsigned long	mask,
		   _Xconst XRenderPictFormat	*template,
		   int			count)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    int		    nf;
    XRenderInfo     *xri;

    RenderCheckExtension (dpy, info, NULL);
    if (!XRenderQueryFormats (dpy))
	return NULL;
    xri = info->info;
    for (nf = 0; nf < xri->nformat; nf++)
    {
	if (mask & PictFormatID)
	    if (template->id != xri->format[nf].id)
		continue;
	if (mask & PictFormatType)
	if (template->type != xri->format[nf].type)
		continue;
	if (mask & PictFormatDepth)
	    if (template->depth != xri->format[nf].depth)
		continue;
	if (mask & PictFormatRed)
	    if (template->direct.red != xri->format[nf].direct.red)
		continue;
	if (mask & PictFormatRedMask)
	    if (template->direct.redMask != xri->format[nf].direct.redMask)
		continue;
	if (mask & PictFormatGreen)
	    if (template->direct.green != xri->format[nf].direct.green)
		continue;
	if (mask & PictFormatGreenMask)
	    if (template->direct.greenMask != xri->format[nf].direct.greenMask)
		continue;
	if (mask & PictFormatBlue)
	    if (template->direct.blue != xri->format[nf].direct.blue)
		continue;
	if (mask & PictFormatBlueMask)
	    if (template->direct.blueMask != xri->format[nf].direct.blueMask)
		continue;
	if (mask & PictFormatAlpha)
	    if (template->direct.alpha != xri->format[nf].direct.alpha)
		continue;
	if (mask & PictFormatAlphaMask)
	    if (template->direct.alphaMask != xri->format[nf].direct.alphaMask)
		continue;
	if (mask & PictFormatColormap)
	    if (template->colormap != xri->format[nf].colormap)
		continue;
	if (count-- == 0)
	    return &xri->format[nf];
    }
    return NULL;
}

XRenderPictFormat *
XRenderFindStandardFormat (Display  *dpy,
			   int	    format)
{
    static struct {
	XRenderPictFormat   templ;
	unsigned long	    mask;
    } standardFormats[PictStandardNUM] = {
	/* PictStandardARGB32 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		32,			    /* depth */
		{			    /* direct */
		    16,			    /* direct.red */
		    0xff,		    /* direct.redMask */
		    8,			    /* direct.green */
		    0xff,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0xff,		    /* direct.blueMask */
		    24,			    /* direct.alpha */
		    0xff,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRed |
	    PictFormatRedMask |
	    PictFormatGreen |
	    PictFormatGreenMask |
	    PictFormatBlue |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
	/* PictStandardRGB24 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		24,			    /* depth */
		{			    /* direct */
		    16,			    /* direct.red */
		    0xff,		    /* direct.redMask */
		    8,			    /* direct.green */
		    0xff,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0xff,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0x00,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRed |
	    PictFormatRedMask |
	    PictFormatGreen |
	    PictFormatGreenMask |
	    PictFormatBlue |
	    PictFormatBlueMask |
	    PictFormatAlphaMask,
	},
	/* PictStandardA8 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		8,			    /* depth */
		{			    /* direct */
		    0,			    /* direct.red */
		    0x00,		    /* direct.redMask */
		    0,			    /* direct.green */
		    0x00,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0x00,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0xff,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRedMask |
	    PictFormatGreenMask |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
	/* PictStandardA4 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		4,			    /* depth */
		{			    /* direct */
		    0,			    /* direct.red */
		    0x00,		    /* direct.redMask */
		    0,			    /* direct.green */
		    0x00,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0x00,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0x0f,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRedMask |
	    PictFormatGreenMask |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
	/* PictStandardA1 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		1,			    /* depth */
		{			    /* direct */
		    0,			    /* direct.red */
		    0x00,		    /* direct.redMask */
		    0,			    /* direct.green */
		    0x00,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0x00,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0x01,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRedMask |
	    PictFormatGreenMask |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
    };

    if (0 <= format && format < PictStandardNUM)
	return XRenderFindFormat (dpy,
				  standardFormats[format].mask,
				  &standardFormats[format].templ,
				  0);
    return NULL;
}

XIndexValue *
XRenderQueryPictIndexValues(Display			*dpy,
			    _Xconst XRenderPictFormat	*format,
			    int				*num)
{
    XRenderExtDisplayInfo		*info = XRenderFindDisplay (dpy);
    xRenderQueryPictIndexValuesReq	*req;
    xRenderQueryPictIndexValuesReply	rep;
    XIndexValue				*values;
    unsigned int			nbytes, nread, i;

    RenderCheckExtension (dpy, info, NULL);

    LockDisplay (dpy);
    GetReq (RenderQueryPictIndexValues, req);
    req->reqType       = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderQueryPictIndexValues;
    req->format        = (CARD32) format->id;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse))
    {
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }

    if ((rep.length < (INT_MAX >> 2)) &&
	(rep.numIndexValues < (INT_MAX / sizeof (XIndexValue)))) {
	unsigned int rlength;
	/* request data length */
	nbytes = rep.length << 2;
	/* bytes of actual data in the request */
	nread = rep.numIndexValues * SIZEOF (xIndexValue);
	/* size of array returned to application */
	rlength = (unsigned) ((unsigned long) rep.numIndexValues * sizeof (XIndexValue));

	/* allocate returned data */
	values = Xmalloc (rlength);
    } else {
	nbytes = nread = 0;
	values = NULL;
    }

    if (!values)
    {
	_XEatDataWords (dpy, rep.length);
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }

    /* read the values one at a time and convert */
    *num = (int) rep.numIndexValues;
    for (i = 0; i < rep.numIndexValues; i++)
    {
	xIndexValue value;

	_XRead (dpy, (char *) &value, SIZEOF (xIndexValue));
	values[i].pixel = value.pixel;
	values[i].red = value.red;
	values[i].green = value.green;
	values[i].blue = value.blue;
	values[i].alpha = value.alpha;
    }
    /* skip any padding */
    if(nbytes > nread)
    {
	_XEatData (dpy, (unsigned long) (nbytes - nread));
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    return values;
}
