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
#include <X11/Xregion.h>

static void
_XRenderProcessPictureAttributes (Display		    *dpy,
				  xRenderChangePictureReq   *req,
				  unsigned long		    valuemask,
				  _Xconst XRenderPictureAttributes  *attributes)
{
    unsigned long values[32];
    register unsigned long *value = values;
    unsigned int nvalues;

    if (valuemask & CPRepeat)
	*value++ = (unsigned long) attributes->repeat;
    if (valuemask & CPAlphaMap)
	*value++ = attributes->alpha_map;
    if (valuemask & CPAlphaXOrigin)
	*value++ = (unsigned long) attributes->alpha_x_origin;
    if (valuemask & CPAlphaYOrigin)
	*value++ = (unsigned long) attributes->alpha_y_origin;
    if (valuemask & CPClipXOrigin)
	*value++ = (unsigned long) attributes->clip_x_origin;
    if (valuemask & CPClipYOrigin)
	*value++ = (unsigned long) attributes->clip_y_origin;
    if (valuemask & CPClipMask)
	*value++ = attributes->clip_mask;
    if (valuemask & CPGraphicsExposure)
	*value++ = (unsigned long) attributes->graphics_exposures;
    if (valuemask & CPSubwindowMode)
	*value++ = (unsigned long) attributes->subwindow_mode;
    if (valuemask & CPPolyEdge)
	*value++ = (unsigned long) attributes->poly_edge;
    if (valuemask & CPPolyMode)
	*value++ = (unsigned long) attributes->poly_mode;
    if (valuemask & CPDither)
	*value++ = attributes->dither;
    if (valuemask & CPComponentAlpha)
	*value++ = (unsigned long) attributes->component_alpha;

    req->length += (nvalues = (unsigned) (value - values));

    nvalues <<= 2;			    /* watch out for macros... */
    Data32 (dpy, (long *) values, (long)nvalues);
}

Picture
XRenderCreatePicture (Display			*dpy,
		      Drawable			drawable,
		      _Xconst XRenderPictFormat		*format,
		      unsigned long		valuemask,
		      _Xconst XRenderPictureAttributes	*attributes)
{
    XRenderExtDisplayInfo   *info = XRenderFindDisplay (dpy);
    Picture		    pid;
    xRenderCreatePictureReq *req;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreatePicture, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreatePicture;
    req->pid = (CARD32) (pid = XAllocID(dpy));
    req->drawable = (CARD32) drawable;
    req->format = (CARD32) format->id;
    if ((req->mask = (CARD32) valuemask))
	_XRenderProcessPictureAttributes (dpy,
					  (xRenderChangePictureReq *) req,
					  valuemask,
					  attributes);
    UnlockDisplay(dpy);
    SyncHandle();
    return pid;
}

void
XRenderChangePicture (Display                   *dpy,
		      Picture			picture,
		      unsigned long             valuemask,
		      _Xconst XRenderPictureAttributes  *attributes)
{
    XRenderExtDisplayInfo    *info = XRenderFindDisplay (dpy);
    xRenderChangePictureReq  *req;

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);
    GetReq(RenderChangePicture, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderChangePicture;
    req->picture = (CARD32) picture;
    req->mask = (CARD32) valuemask;
    _XRenderProcessPictureAttributes (dpy,
				      req,
				      valuemask,
				      attributes);
    UnlockDisplay(dpy);
    SyncHandle();
}

static void
_XRenderSetPictureClipRectangles (Display	    *dpy,
				  XRenderExtDisplayInfo   *info,
				  Picture	    picture,
				  int		    xOrigin,
				  int		    yOrigin,
				  _Xconst XRectangle	    *rects,
				  int		    n)
{
    xRenderSetPictureClipRectanglesReq	*req;
    long				len;

    GetReq (RenderSetPictureClipRectangles, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderSetPictureClipRectangles;
    req->picture = (CARD32) picture;
    req->xOrigin = (INT16) xOrigin;
    req->yOrigin = (INT16) yOrigin;
    len = ((long) n) << 1;
    SetReqLen (req, len, 1);
    len <<= 2;
    Data16 (dpy, (_Xconst short *) rects, len);
}

void
XRenderSetPictureClipRectangles (Display	*dpy,
				 Picture	picture,
				 int		xOrigin,
				 int		yOrigin,
				 _Xconst XRectangle	*rects,
				 int		n)
{
    XRenderExtDisplayInfo	    *info = XRenderFindDisplay (dpy);

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);
    _XRenderSetPictureClipRectangles (dpy, info, picture,
				      xOrigin, yOrigin, rects, n);
    UnlockDisplay (dpy);
    SyncHandle ();
}

void
XRenderSetPictureClipRegion (Display	    *dpy,
			     Picture	    picture,
			     Region	    r)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    XRectangle	    *xr;
    unsigned long   total;

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);
    total = (unsigned long) ((size_t) r->numRects * sizeof (XRectangle));
    if ((xr = (XRectangle *) _XAllocTemp(dpy, total))) {
	int		i;
	XRectangle	*pr;
	BOX		*pb;

	for (pr = xr, pb = r->rects, i = (int) r->numRects; --i >= 0; pr++, pb++) {
	    pr->x = pb->x1;
	    pr->y = pb->y1;
	    pr->width = (unsigned short) (pb->x2 - pb->x1);
	    pr->height = (unsigned short) (pb->y2 - pb->y1);
	}
    }
    if (xr || !r->numRects)
	_XRenderSetPictureClipRectangles (dpy, info, picture, 0, 0,
					  xr, (int) r->numRects);
    if (xr)
	_XFreeTemp(dpy, (char *)xr, total);
    UnlockDisplay(dpy);
    SyncHandle();
}

void
XRenderSetPictureTransform (Display	*dpy,
			    Picture	picture,
			    XTransform	*transform)
{
    XRenderExtDisplayInfo	    *info = XRenderFindDisplay (dpy);
    xRenderSetPictureTransformReq   *req;

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay (dpy);
    GetReq(RenderSetPictureTransform, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderSetPictureTransform;
    req->picture = (CARD32) picture;
    req->transform.matrix11 = transform->matrix[0][0];
    req->transform.matrix12 = transform->matrix[0][1];
    req->transform.matrix13 = transform->matrix[0][2];
    req->transform.matrix21 = transform->matrix[1][0];
    req->transform.matrix22 = transform->matrix[1][1];
    req->transform.matrix23 = transform->matrix[1][2];
    req->transform.matrix31 = transform->matrix[2][0];
    req->transform.matrix32 = transform->matrix[2][1];
    req->transform.matrix33 = transform->matrix[2][2];
    UnlockDisplay(dpy);
    SyncHandle();

}

void
XRenderFreePicture (Display                   *dpy,
		    Picture                   picture)
{
    XRenderExtDisplayInfo   *info = XRenderFindDisplay (dpy);
    xRenderFreePictureReq   *req;

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);
    GetReq(RenderFreePicture, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderFreePicture;
    req->picture = (CARD32) picture;
    UnlockDisplay(dpy);
    SyncHandle();
}


Picture XRenderCreateSolidFill(Display *dpy,
                               const XRenderColor *color)
{
    XRenderExtDisplayInfo	    *info = XRenderFindDisplay (dpy);
    Picture			    pid;
    xRenderCreateSolidFillReq	    *req;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreateSolidFill, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreateSolidFill;

    req->pid = (CARD32) (pid = XAllocID(dpy));
    req->color.red = color->red;
    req->color.green = color->green;
    req->color.blue = color->blue;
    req->color.alpha = color->alpha;

    UnlockDisplay(dpy);
    SyncHandle();
    return pid;
}


Picture XRenderCreateLinearGradient(Display *dpy,
                                    const XLinearGradient *gradient,
                                    const XFixed *stops,
                                    const XRenderColor *colors,
                                    int nStops)
{
    XRenderExtDisplayInfo	    *info = XRenderFindDisplay (dpy);
    Picture			    pid;
    xRenderCreateLinearGradientReq *req;
    long			   len;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreateLinearGradient, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreateLinearGradient;

    req->pid = (CARD32) (pid = XAllocID(dpy));
    req->p1.x = gradient->p1.x;
    req->p1.y = gradient->p1.y;
    req->p2.x = gradient->p2.x;
    req->p2.y = gradient->p2.y;

    req->nStops = (CARD32) nStops;
    len = (long) nStops * 3;
    SetReqLen (req, len, 6);
    DataInt32(dpy, stops, nStops * 4);
    Data16(dpy, colors, nStops * 8);

    UnlockDisplay(dpy);
    SyncHandle();
    return pid;
}

Picture XRenderCreateRadialGradient(Display *dpy,
                                    const XRadialGradient *gradient,
                                    const XFixed *stops,
                                    const XRenderColor *colors,
                                    int nStops)
{
    XRenderExtDisplayInfo	    *info = XRenderFindDisplay (dpy);
    Picture			    pid;
    xRenderCreateRadialGradientReq *req;
    long			   len;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreateRadialGradient, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreateRadialGradient;

    req->pid = (CARD32) (pid = XAllocID(dpy));
    req->inner.x = gradient->inner.x;
    req->inner.y = gradient->inner.y;
    req->outer.x = gradient->outer.x;
    req->outer.y = gradient->outer.y;
    req->inner_radius = gradient->inner.radius;
    req->outer_radius = gradient->outer.radius;

    req->nStops = (CARD32) nStops;
    len = (long) nStops * 3;
    SetReqLen (req, len, 6);
    DataInt32(dpy, stops, nStops * 4);
    Data16(dpy, colors, nStops * 8);

    UnlockDisplay(dpy);
    SyncHandle();
    return pid;
}

Picture XRenderCreateConicalGradient(Display *dpy,
                                     const XConicalGradient *gradient,
                                     const XFixed *stops,
                                     const XRenderColor *colors,
                                     int nStops)
{
    XRenderExtDisplayInfo	    *info = XRenderFindDisplay (dpy);
    Picture			    pid;
    xRenderCreateConicalGradientReq *req;
    long			    len;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreateConicalGradient, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreateConicalGradient;

    req->pid = (CARD32) (pid = XAllocID(dpy));
    req->center.x = gradient->center.x;
    req->center.y = gradient->center.y;
    req->angle = gradient->angle;

    req->nStops = (CARD32) nStops;
    len = (long) nStops * 3;
    SetReqLen (req, len, 6);
    DataInt32(dpy, stops, nStops * 4);
    Data16(dpy, colors, nStops * 8);

    UnlockDisplay(dpy);
    SyncHandle();
    return pid;
}
