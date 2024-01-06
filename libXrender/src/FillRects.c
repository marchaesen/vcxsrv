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

/* precompute the maximum size of batching request allowed */

#define size (SIZEOF(xRenderFillRectanglesReq) + FRCTSPERBATCH * SIZEOF(xRectangle))

void
XRenderFillRectangles (Display		    *dpy,
		       int		    op,
		       Picture		    dst,
		       _Xconst XRenderColor *color,
		       _Xconst XRectangle   *rectangles,
		       int		    n_rects)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    xRenderFillRectanglesReq	*req;

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);

    while (n_rects)
    {
	long	len;
	int	n;

	GetReq(RenderFillRectangles, req);

	req->reqType = (CARD8) info->codes->major_opcode;
	req->renderReqType = X_RenderFillRectangles;
	req->op = (CARD8) op;
	req->dst = (CARD32) dst;
	req->color.red = color->red;
	req->color.green = color->green;
	req->color.blue = color->blue;
	req->color.alpha = color->alpha;

	n = n_rects;
	len = ((long)n) << 1;
	if (!dpy->bigreq_size && len > (dpy->max_request_size - req->length))
	{
	    n = (int) ((dpy->max_request_size - req->length) >> 1);
	    len = ((long)n) << 1;
	}
	SetReqLen(req, len, len);
	len <<= 2; /* watch out for macros... */
	Data16 (dpy, (_Xconst short *) rectangles, len);
	n_rects -= n;
	rectangles += n;
    }
    UnlockDisplay(dpy);
    SyncHandle();
}

