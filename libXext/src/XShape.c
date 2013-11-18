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
 * Author:  Keith Packard, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xlibint.h>
#include <X11/Xutil.h>
#include <X11/Xregion.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/shapeproto.h>
#include <limits.h>

static XExtensionInfo _shape_info_data;
static XExtensionInfo *shape_info = &_shape_info_data;
static const char *shape_extension_name = SHAPENAME;

#define ShapeCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, shape_extension_name, val)
#define ShapeSimpleCheckExtension(dpy,i) \
  XextSimpleCheckExtension (dpy, i, shape_extension_name)


/*****************************************************************************
 *                                                                           *
 *			   private utility routines                          *
 *                                                                           *
 *****************************************************************************/

static int close_display(Display *dpy, XExtCodes *codes);
static Bool wire_to_event (Display *dpy, XEvent *re, xEvent *event);
static Status event_to_wire (Display *dpy, XEvent *re, xEvent *event);
static /* const */ XExtensionHooks shape_extension_hooks = {
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
    NULL,				/* error_string */
};

static XEXT_GENERATE_FIND_DISPLAY (find_display, shape_info,
				   shape_extension_name,
				   &shape_extension_hooks,
				   ShapeNumberEvents, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, shape_info)


static Bool
wire_to_event (Display *dpy, XEvent *re, xEvent *event)
{
    XExtDisplayInfo *info = find_display (dpy);
    XShapeEvent		*se;
    xShapeNotifyEvent	*sevent;

    ShapeCheckExtension (dpy, info, False);

    switch ((event->u.u.type & 0x7f) - info->codes->first_event) {
    case ShapeNotify:
    	se = (XShapeEvent *) re;
	sevent = (xShapeNotifyEvent *) event;
    	se->type = sevent->type & 0x7f;
    	se->serial = _XSetLastRequestRead(dpy,(xGenericReply *) event);
    	se->send_event = (sevent->type & 0x80) != 0;
    	se->display = dpy;
    	se->window = sevent->window;
    	se->kind = sevent->kind;
    	se->x = cvtINT16toInt (sevent->x);
    	se->y = cvtINT16toInt (sevent->y);
    	se->width = sevent->width;
    	se->height = sevent->height;
	se->time = sevent->time;
	se->shaped = True;
	if (sevent->shaped == xFalse)
	    se->shaped = False;
    	return True;
    }
    return False;
}

static Status
event_to_wire (Display *dpy, XEvent *re, xEvent *event)
{
    XExtDisplayInfo *info = find_display (dpy);
    XShapeEvent		*se;
    xShapeNotifyEvent	*sevent;

    ShapeCheckExtension (dpy, info, 0);

    switch ((re->type & 0x7f) - info->codes->first_event) {
    case ShapeNotify:
    	se = (XShapeEvent *) re;
	sevent = (xShapeNotifyEvent *) event;
    	sevent->type = se->type | (se->send_event ? 0x80 : 0);
    	sevent->sequenceNumber = se->serial & 0xffff;
    	sevent->window = se->window;
    	sevent->kind = se->kind;
    	sevent->x = se->x;
    	sevent->y = se->y;
    	sevent->width = se->width;
    	sevent->height = se->height;
	sevent->time = se->time;
    	return 1;
    }
    return 0;
}


/****************************************************************************
 *                                                                          *
 *			    Shape public interfaces                         *
 *                                                                          *
 ****************************************************************************/

Bool XShapeQueryExtension (Display *dpy, int *event_basep, int *error_basep)
{
    XExtDisplayInfo *info = find_display (dpy);

    if (XextHasExtension(info)) {
	*event_basep = info->codes->first_event;
	*error_basep = info->codes->first_error;
	return True;
    } else {
	return False;
    }
}


Status XShapeQueryVersion(
    Display *dpy,
    int *major_versionp,
    int *minor_versionp)
{
    XExtDisplayInfo *info = find_display (dpy);
    xShapeQueryVersionReply	    rep;
    register xShapeQueryVersionReq  *req;

    ShapeCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    GetReq (ShapeQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeQueryVersion;
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    *major_versionp = rep.majorVersion;
    *minor_versionp = rep.minorVersion;
    UnlockDisplay (dpy);
    SyncHandle ();
    return 1;
}

void XShapeCombineRegion(
    register Display *dpy,
    Window dest,
    int destKind, int xOff, int yOff,
    register REGION *r,
    int op)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeRectanglesReq *req;
    register long nbytes;
    register int i;
    register XRectangle *xr, *pr;
    register BOX *pb;

    ShapeSimpleCheckExtension (dpy, info);

    LockDisplay(dpy);
    GetReq(ShapeRectangles, req);
    xr = (XRectangle *)
    	_XAllocScratch(dpy, (unsigned long)(r->numRects * sizeof (XRectangle)));
    for (pr = xr, pb = r->rects, i = r->numRects; --i >= 0; pr++, pb++) {
        pr->x = pb->x1;
	pr->y = pb->y1;
	pr->width = pb->x2 - pb->x1;
	pr->height = pb->y2 - pb->y1;
     }
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeRectangles;
    req->op = op;
    req->ordering = YXBanded;
    req->destKind = destKind;
    req->dest = dest;
    req->xOff = xOff;
    req->yOff = yOff;

    /* SIZEOF(xRectangle) will be a multiple of 4 */
    req->length += r->numRects * (SIZEOF(xRectangle) / 4);

    nbytes = r->numRects * sizeof(xRectangle);

    Data16 (dpy, (short *) xr, nbytes);
    UnlockDisplay(dpy);
    SyncHandle();
}


void XShapeCombineRectangles (
    register Display *dpy,
    XID dest,
    int destKind, int xOff, int yOff,
    XRectangle *rects,
    int n_rects,
    int op, int ordering)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeRectanglesReq *req;
    register long nbytes;

    ShapeSimpleCheckExtension (dpy, info);

    LockDisplay(dpy);
    GetReq(ShapeRectangles, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeRectangles;
    req->op = op;
    req->ordering = ordering;
    req->destKind = destKind;
    req->dest = dest;
    req->xOff = xOff;
    req->yOff = yOff;

    /* SIZEOF(xRectangle) will be a multiple of 4 */
    req->length += n_rects * (SIZEOF(xRectangle) / 4);

    nbytes = n_rects * sizeof(xRectangle);

    Data16 (dpy, (short *) rects, nbytes);
    UnlockDisplay(dpy);
    SyncHandle();
}


void XShapeCombineMask (
    register Display *dpy,
    XID dest,
    int destKind,
    int xOff, int yOff,
    Pixmap src,
    int op)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeMaskReq *req;

    ShapeSimpleCheckExtension (dpy, info);

    LockDisplay(dpy);
    GetReq(ShapeMask, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeMask;
    req->op = op;
    req->destKind = destKind;
    req->dest = dest;
    req->xOff = xOff;
    req->yOff = yOff;
    req->src = src;
    UnlockDisplay(dpy);
    SyncHandle();
}

void XShapeCombineShape (
    register Display *dpy,
    XID dest,
    int destKind,
    int xOff, int yOff,
    XID src,
    int srcKind,
    int op)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeCombineReq *req;

    ShapeSimpleCheckExtension (dpy, info);

    LockDisplay(dpy);
    GetReq(ShapeCombine, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeCombine;
    req->op = op;
    req->destKind = destKind;
    req->srcKind = srcKind;
    req->dest = dest;
    req->xOff = xOff;
    req->yOff = yOff;
    req->src = src;
    UnlockDisplay(dpy);
    SyncHandle();
}

void XShapeOffsetShape (
    register Display *dpy,
    XID dest,
    int destKind,
    int xOff, int yOff)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeOffsetReq *req;

    ShapeSimpleCheckExtension (dpy, info);

    LockDisplay(dpy);
    GetReq(ShapeOffset, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeOffset;
    req->destKind = destKind;
    req->dest = dest;
    req->xOff = xOff;
    req->yOff = yOff;
    UnlockDisplay(dpy);
    SyncHandle();
}

Status XShapeQueryExtents (
    register Display *dpy,
    Window window,
    int *bShaped, int *xbs, int *ybs, unsigned int *wbs, unsigned int *hbs, /* RETURN */
    int *cShaped, int *xcs, int *ycs, unsigned int *wcs, unsigned int *hcs /* RETURN */)
{
    XExtDisplayInfo *info = find_display (dpy);
    xShapeQueryExtentsReply	    rep;
    register xShapeQueryExtentsReq *req;

    ShapeCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    GetReq (ShapeQueryExtents, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeQueryExtents;
    req->window = window;
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    *bShaped = rep.boundingShaped;
    *cShaped = rep.clipShaped;
    *xbs = cvtINT16toInt (rep.xBoundingShape);
    *ybs = cvtINT16toInt (rep.yBoundingShape);
    *wbs = rep.widthBoundingShape;
    *hbs = rep.heightBoundingShape;
    *xcs = cvtINT16toInt (rep.xClipShape);
    *ycs = cvtINT16toInt (rep.yClipShape);
    *wcs = rep.widthClipShape;
    *hcs = rep.heightClipShape;
    UnlockDisplay (dpy);
    SyncHandle ();
    return 1;
}


void XShapeSelectInput (
    register Display *dpy,
    Window window,
    unsigned long mask)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeSelectInputReq   *req;

    ShapeSimpleCheckExtension (dpy, info);

    LockDisplay (dpy);
    GetReq (ShapeSelectInput, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeSelectInput;
    req->window = window;
    if (mask & ShapeNotifyMask)
	req->enable = xTrue;
    else
	req->enable = xFalse;
    UnlockDisplay (dpy);
    SyncHandle ();
}

unsigned long XShapeInputSelected (register Display *dpy, Window window)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeInputSelectedReq *req;
    xShapeInputSelectedReply	    rep;

    ShapeCheckExtension (dpy, info, False);

    LockDisplay (dpy);
    GetReq (ShapeInputSelected, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeInputSelected;
    req->window = window;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return False;
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    return rep.enabled ? ShapeNotifyMask : 0L;
}


XRectangle *XShapeGetRectangles (
    register Display *dpy,
    Window window,
    int kind,
    int *count, /* RETURN */
    int *ordering /* RETURN */)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShapeGetRectanglesReq   *req;
    xShapeGetRectanglesReply	    rep;
    XRectangle			    *rects;
    xRectangle			    *xrects;
    unsigned int		    i;

    ShapeCheckExtension (dpy, info, (XRectangle *)NULL);

    LockDisplay (dpy);
    GetReq (ShapeGetRectangles, req);
    req->reqType = info->codes->major_opcode;
    req->shapeReqType = X_ShapeGetRectangles;
    req->window = window;
    req->kind = kind;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return (XRectangle *)NULL;
    }
    *count = rep.nrects;
    *ordering = rep.ordering;
    rects = NULL;
    if (rep.nrects) {
	if (rep.nrects < (INT_MAX / sizeof (XRectangle))) {
	    xrects = Xmalloc (rep.nrects * sizeof (xRectangle));
	    rects = Xmalloc (rep.nrects * sizeof (XRectangle));
	} else {
	    xrects = NULL;
	    rects = NULL;
	}
	if (!xrects || !rects) {
	    Xfree (xrects);
	    Xfree (rects);
	    _XEatDataWords (dpy, rep.length);
	    rects = NULL;
	    *count = 0;
	} else {
	    _XRead (dpy, (char *) xrects, rep.nrects * sizeof (xRectangle));
	    for (i = 0; i < rep.nrects; i++) {
	    	rects[i].x = (short) cvtINT16toInt (xrects[i].x);
	    	rects[i].y = (short) cvtINT16toInt (xrects[i].y);
	    	rects[i].width = xrects[i].width;
	    	rects[i].height = xrects[i].height;
	    }
	    Xfree (xrects);
	}
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    return rects;
}
