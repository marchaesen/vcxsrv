/*
 *
 * Copyright © 2002 Keith Packard
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
#include "Xrenderint.h"

Cursor
XRenderCreateCursor (Display	    *dpy,
		     Picture	    source,
		     unsigned int   x,
		     unsigned int   y)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    Cursor			cid;
    xRenderCreateCursorReq	*req;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreateCursor, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreateCursor;
    req->cid = (CARD32) (cid = XAllocID (dpy));
    req->src = (CARD32) source;
    req->x = (CARD16) x;
    req->y = (CARD16) y;

    UnlockDisplay(dpy);
    SyncHandle();
    return cid;
}

Cursor
XRenderCreateAnimCursor (Display	*dpy,
			 int		ncursor,
			 XAnimCursor	*cursors)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    Cursor			cid;
    xRenderCreateAnimCursorReq	*req;
    long			len;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreateAnimCursor, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreateAnimCursor;
    req->cid = (CARD32) (cid = XAllocID (dpy));

    len = (long) ncursor * SIZEOF (xAnimCursorElt) >> 2;
    SetReqLen (req, len, len);
    len <<= 2;
    Data32 (dpy, (long *) cursors, len);

    UnlockDisplay(dpy);
    SyncHandle();
    return cid;
}
