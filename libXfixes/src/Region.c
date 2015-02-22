/*
 * Copyright © 2003 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xfixesint.h"

XserverRegion
XFixesCreateRegion (Display *dpy, XRectangle *rectangles, int nrectangles)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesCreateRegionReq	*req;
    long    			len;
    XserverRegion		region;

    XFixesCheckExtension (dpy, info, 0);
    LockDisplay (dpy);
    GetReq (XFixesCreateRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegion;
    region = req->region = XAllocID (dpy);
    len = ((long) nrectangles) << 1;
    SetReqLen (req, len, len);
    len <<= 2;
    Data16 (dpy, (short *) rectangles, len);
    UnlockDisplay (dpy);
    SyncHandle();
    return region;
}

XserverRegion
XFixesCreateRegionFromBitmap (Display *dpy, Pixmap bitmap)
{
    XFixesExtDisplayInfo		*info = XFixesFindDisplay (dpy);
    xXFixesCreateRegionFromBitmapReq	*req;
    XserverRegion			region;

    XFixesCheckExtension (dpy, info, 0);
    LockDisplay (dpy);
    GetReq (XFixesCreateRegionFromBitmap, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromBitmap;
    region = req->region = XAllocID (dpy);
    req->bitmap = bitmap;
    UnlockDisplay (dpy);
    SyncHandle();
    return region;
}

XserverRegion
XFixesCreateRegionFromWindow (Display *dpy, Window window, int kind)
{
    XFixesExtDisplayInfo		*info = XFixesFindDisplay (dpy);
    xXFixesCreateRegionFromWindowReq	*req;
    XserverRegion			region;

    XFixesCheckExtension (dpy, info, 0);
    LockDisplay (dpy);
    GetReq (XFixesCreateRegionFromWindow, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromWindow;
    region = req->region = XAllocID (dpy);
    req->window = window;
    req->kind = kind;
    UnlockDisplay (dpy);
    SyncHandle();
    return region;
}

XserverRegion
XFixesCreateRegionFromGC (Display *dpy, GC gc)
{
    XFixesExtDisplayInfo		*info = XFixesFindDisplay (dpy);
    xXFixesCreateRegionFromGCReq	*req;
    XserverRegion			region;

    XFixesCheckExtension (dpy, info, 0);
    LockDisplay (dpy);
    GetReq (XFixesCreateRegionFromGC, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromGC;
    region = req->region = XAllocID (dpy);
    req->gc = gc->gid;
    UnlockDisplay (dpy);
    SyncHandle();
    return region;
}

XserverRegion
XFixesCreateRegionFromPicture (Display *dpy, XID picture)
{
    XFixesExtDisplayInfo		*info = XFixesFindDisplay (dpy);
    xXFixesCreateRegionFromPictureReq	*req;
    XserverRegion			region;

    XFixesCheckExtension (dpy, info, 0);
    LockDisplay (dpy);
    GetReq (XFixesCreateRegionFromPicture, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromPicture;
    region = req->region = XAllocID (dpy);
    req->picture = picture;
    UnlockDisplay (dpy);
    SyncHandle();
    return region;
}

void
XFixesDestroyRegion (Display *dpy, XserverRegion region)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesDestroyRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesDestroyRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesDestroyRegion;
    req->region = region;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesSetRegion (Display *dpy, XserverRegion region,
		 XRectangle *rectangles, int nrectangles)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesSetRegionReq		*req;
    long    			len;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesSetRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetRegion;
    req->region = region;
    len = ((long) nrectangles) << 1;
    SetReqLen (req, len, len);
    len <<= 2;
    Data16 (dpy, (short *) rectangles, len);
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesCopyRegion (Display *dpy, XserverRegion dst, XserverRegion src)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesCopyRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesCopyRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCopyRegion;
    req->source = src;
    req->destination = dst;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesUnionRegion (Display *dpy, XserverRegion dst,
		   XserverRegion src1, XserverRegion src2)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesUnionRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesUnionRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesUnionRegion;
    req->source1 = src1;
    req->source2 = src2;
    req->destination = dst;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesIntersectRegion (Display *dpy, XserverRegion dst,
		       XserverRegion src1, XserverRegion src2)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesIntersectRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesIntersectRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesIntersectRegion;
    req->source1 = src1;
    req->source2 = src2;
    req->destination = dst;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesSubtractRegion (Display *dpy, XserverRegion dst,
		      XserverRegion src1, XserverRegion src2)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesSubtractRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesSubtractRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSubtractRegion;
    req->source1 = src1;
    req->source2 = src2;
    req->destination = dst;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesInvertRegion (Display *dpy, XserverRegion dst,
		    XRectangle *rect, XserverRegion src)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesInvertRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesInvertRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesInvertRegion;
    req->x = rect->x;
    req->y = rect->y;
    req->width = rect->width;
    req->height = rect->height;
    req->source = src;
    req->destination = dst;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesTranslateRegion (Display *dpy, XserverRegion region, int dx, int dy)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesTranslateRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesTranslateRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesTranslateRegion;
    req->region = region;
    req->dx = dx;
    req->dy = dy;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesRegionExtents (Display *dpy, XserverRegion dst, XserverRegion src)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesRegionExtentsReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesRegionExtents, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesRegionExtents;
    req->source = src;
    req->destination = dst;
    UnlockDisplay (dpy);
    SyncHandle();
}

XRectangle *
XFixesFetchRegion (Display *dpy, XserverRegion region, int *nrectanglesRet)
{
    XRectangle	bounds;

    return XFixesFetchRegionAndBounds (dpy, region, nrectanglesRet, &bounds);
}

XRectangle *
XFixesFetchRegionAndBounds (Display	    *dpy, 
			    XserverRegion   region, 
			    int		    *nrectanglesRet,
			    XRectangle	    *bounds)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesFetchRegionReq	*req;
    xXFixesFetchRegionReply	rep;
    XRectangle			*rects;
    int    			nrects;
    long    			nbytes;
    long			nread;

    XFixesCheckExtension (dpy, info, NULL);
    LockDisplay (dpy);
    GetReq (XFixesFetchRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesFetchRegion;
    req->region = region;
    *nrectanglesRet = 0;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse))
    {
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }
    bounds->x = rep.x;
    bounds->y = rep.y;
    bounds->width = rep.width;
    bounds->height = rep.height;
    nbytes = (long) rep.length << 2;
    nrects = rep.length >> 1;
    nread = nrects << 3;
    rects = Xmalloc (nrects * sizeof (XRectangle));
    if (!rects)
    {
	_XEatData (dpy, nbytes);
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }
    _XRead16 (dpy, (short *) rects, nrects << 3);
    /* skip any padding */
    if(nbytes > nread)
    {
	_XEatData (dpy, (unsigned long) (nbytes - nread));
    }
    UnlockDisplay (dpy);
    SyncHandle();
    *nrectanglesRet = nrects;
    return rects;
}

void
XFixesSetGCClipRegion (Display *dpy, GC gc, 
		       int clip_x_origin, int clip_y_origin,
		       XserverRegion region)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesSetGCClipRegionReq	    *req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesSetGCClipRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetGCClipRegion;
    req->gc = gc->gid;
    req->region = region;
    req->xOrigin = clip_x_origin;
    req->yOrigin = clip_y_origin;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesSetWindowShapeRegion (Display *dpy, Window win, int shape_kind,
			    int x_off, int y_off, XserverRegion region)
{
    XFixesExtDisplayInfo	    *info = XFixesFindDisplay (dpy);
    xXFixesSetWindowShapeRegionReq  *req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesSetWindowShapeRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetWindowShapeRegion;
    req->dest = win;
    req->destKind = shape_kind;
    req->xOff = x_off;
    req->yOff = y_off;
    req->region = region;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesSetPictureClipRegion (Display *dpy, XID picture,
			    int clip_x_origin, int clip_y_origin,
			    XserverRegion region)
{
    XFixesExtDisplayInfo	    *info = XFixesFindDisplay (dpy);
    xXFixesSetPictureClipRegionReq  *req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesSetPictureClipRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetPictureClipRegion;
    req->picture = picture;
    req->region = region;
    req->xOrigin = clip_x_origin;
    req->yOrigin = clip_y_origin;
    UnlockDisplay (dpy);
    SyncHandle();
}

void
XFixesExpandRegion (Display *dpy, XserverRegion dst, XserverRegion src,
		    unsigned left, unsigned right,
		    unsigned top, unsigned bottom)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesExpandRegionReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq (XFixesExpandRegion, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesExpandRegion;
    req->source = src;
    req->destination = dst;
    req->left = left;
    req->right = right;
    req->top = top;
    req->bottom = bottom;
    UnlockDisplay (dpy);
    SyncHandle();
}

