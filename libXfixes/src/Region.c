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
#include <limits.h>
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegion;
    region = XAllocID (dpy);
    req->region = (CARD32) region;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromBitmap;
    region = XAllocID (dpy);
    req->region = (CARD32) region;
    req->bitmap = (CARD32) bitmap;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromWindow;
    region = XAllocID (dpy);
    req->region = (CARD32) region;
    req->window = (CARD32) window;
    req->kind = (CARD8) kind;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromGC;
    region = XAllocID (dpy);
    req->region = (CARD32) region;
    req->gc = (CARD32) gc->gid;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreateRegionFromPicture;
    region = XAllocID (dpy);
    req->region = (CARD32) region;
    req->picture = (CARD32) picture;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesDestroyRegion;
    req->region = (CARD32) region;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetRegion;
    req->region = (CARD32) region;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCopyRegion;
    req->source = (CARD32) src;
    req->destination = (CARD32) dst;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesUnionRegion;
    req->source1 = (CARD32) src1;
    req->source2 = (CARD32) src2;
    req->destination = (CARD32) dst;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesIntersectRegion;
    req->source1 = (CARD32) src1;
    req->source2 = (CARD32) src2;
    req->destination = (CARD32) dst;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSubtractRegion;
    req->source1 = (CARD32) src1;
    req->source2 = (CARD32) src2;
    req->destination = (CARD32) dst;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesInvertRegion;
    req->x = rect->x;
    req->y = rect->y;
    req->width = rect->width;
    req->height = rect->height;
    req->source = (CARD32) src;
    req->destination = (CARD32) dst;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesTranslateRegion;
    req->region = (CARD32) region;
    req->dx = (INT16) dx;
    req->dy = (INT16) dy;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesRegionExtents;
    req->source = (CARD32) src;
    req->destination = (CARD32) dst;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesFetchRegion;
    req->region = (CARD32) region;
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

    if (rep.length < (INT_MAX >> 2)) {
	nbytes = (long) rep.length << 2;
	nrects = rep.length >> 1;
	rects = Xmalloc ((size_t) nrects * sizeof (XRectangle));
    } else {
	nbytes = 0;
	nrects = 0;
	rects = NULL;
    }

    if (!rects)
    {
	_XEatDataWords(dpy, rep.length);
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }
    nread = nrects << 3;
    _XRead16 (dpy, (short *) rects, nread);
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetGCClipRegion;
    req->gc = (CARD32) gc->gid;
    req->region = (CARD32) region;
    req->xOrigin = (INT16) clip_x_origin;
    req->yOrigin = (INT16) clip_y_origin;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetWindowShapeRegion;
    req->dest = (CARD32) win;
    req->destKind = (BYTE) shape_kind;
    req->xOff = (INT16) x_off;
    req->yOff = (INT16) y_off;
    req->region = (CARD32) region;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetPictureClipRegion;
    req->picture = (CARD32) picture;
    req->region = (CARD32) region;
    req->xOrigin = (INT16) clip_x_origin;
    req->yOrigin = (INT16) clip_y_origin;
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
    req->reqType = (CARD8) info->codes->major_opcode;
    req->xfixesReqType = X_XFixesExpandRegion;
    req->source = (CARD32) src;
    req->destination = (CARD32) dst;
    req->left = (CARD16) left;
    req->right = (CARD16) right;
    req->top = (CARD16) top;
    req->bottom = (CARD16) bottom;
    UnlockDisplay (dpy);
    SyncHandle();
}

