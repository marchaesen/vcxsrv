/*
 * Copyright © 2004 Keith Packard
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

#define NLOCAL	256

void
XRenderAddTraps (Display	    *dpy,
		 Picture	    picture,
		 int		    xOff,
		 int		    yOff,
		 _Xconst XTrap	    *traps,
		 int		    ntrap)
{
    XRenderExtDisplayInfo   *info = XRenderFindDisplay (dpy);
    unsigned long	    max_req = dpy->bigreq_size ? dpy->bigreq_size : dpy->max_request_size;

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);
    while (ntrap)
    {
	xRenderAddTrapsReq	*req;
	int			n;
	unsigned long		len;

	GetReq(RenderAddTraps, req);
	req->reqType = (CARD8) info->codes->major_opcode;
	req->renderReqType = X_RenderAddTraps;
	req->picture = (CARD32) picture;
	req->xOff = (INT16) xOff;
	req->yOff = (INT16) yOff;
	n = ntrap;
	len = ((unsigned long) n) * (SIZEOF (xTrap) >> 2);
	if (len > (max_req - req->length)) {
	    n = (int) ((max_req - req->length) / (SIZEOF (xTrap) >> 2));
	    len = ((unsigned long) n) * (SIZEOF (xTrap) >> 2);
	}
	SetReqLen (req, len, len);
	len <<= 2;
	DataInt32 (dpy, (_Xconst int *) traps, (long) len);
	ntrap -= n;
	traps += n;
    }
    UnlockDisplay(dpy);
    SyncHandle();
}
