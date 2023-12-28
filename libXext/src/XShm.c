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
 * Author:  Bob Scheifler and Keith Packard, MIT X Consortium
 */

/* THIS IS NOT AN X CONSORTIUM STANDARD OR AN X PROJECT TEAM SPECIFICATION */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/Xlibint.h>
#include <X11/ImUtil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/shmproto.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>

static XExtensionInfo _shm_info_data;
static XExtensionInfo *shm_info = &_shm_info_data;
static const char *shm_extension_name = SHMNAME;

#define ShmCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, shm_extension_name, val)

/*****************************************************************************
 *                                                                           *
 *			   private utility routines                          *
 *                                                                           *
 *****************************************************************************/

static int close_display(Display *dpy, XExtCodes *codes);
static char *error_string(Display *dpy, int code, XExtCodes *codes,
			  char *buf, int n);
static Bool wire_to_event (Display *dpy, XEvent *re, xEvent *event);
static Status event_to_wire (Display *dpy, XEvent *re, xEvent *event);
static /* const */ XExtensionHooks shm_extension_hooks = {
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

static const char *shm_error_list[] = {
    "BadShmSeg",			/* BadShmSeg */
};

static XEXT_GENERATE_FIND_DISPLAY (find_display, shm_info, shm_extension_name,
				   &shm_extension_hooks, ShmNumberEvents, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, shm_info)

static XEXT_GENERATE_ERROR_STRING (error_string, shm_extension_name,
				   ShmNumberErrors, shm_error_list)


static Bool
wire_to_event (Display *dpy, XEvent *re, xEvent *event)
{
    XExtDisplayInfo *info = find_display (dpy);
    XShmCompletionEvent	*se;
    xShmCompletionEvent	*sevent;

    ShmCheckExtension (dpy, info, False);

    switch ((event->u.u.type & 0x7f) - info->codes->first_event) {
    case ShmCompletion:
	se = (XShmCompletionEvent *) re;
	sevent = (xShmCompletionEvent *) event;
	se->type = sevent->type & 0x7f;
	se->serial = _XSetLastRequestRead(dpy,(xGenericReply *) event);
	se->send_event = (sevent->type & 0x80) != 0;
	se->display = dpy;
	se->drawable = sevent->drawable;
	se->major_code = sevent->majorEvent;
	se->minor_code = sevent->minorEvent;
	se->shmseg = sevent->shmseg;
	se->offset = sevent->offset;
    	return True;
    }
    return False;
}

static Status
event_to_wire (Display *dpy, XEvent *re, xEvent *event)
{
    XExtDisplayInfo *info = find_display (dpy);
    XShmCompletionEvent	*se;
    xShmCompletionEvent	*sevent;

    ShmCheckExtension (dpy, info, 0);

    switch ((re->type & 0x7f) - info->codes->first_event) {
    case ShmCompletion:
    	se = (XShmCompletionEvent *) re;
	sevent = (xShmCompletionEvent *) event;
    	sevent->type = se->type | (se->send_event ? 0x80 : 0);
    	sevent->sequenceNumber = se->serial & 0xffff;
    	sevent->drawable = se->drawable;
    	sevent->majorEvent = se->major_code;
    	sevent->minorEvent = se->minor_code;
    	sevent->shmseg = se->shmseg;
    	sevent->offset = se->offset;
    	return True;
    }
    return False;
}

/*****************************************************************************
 *                                                                           *
 *		    public Shared Memory Extension routines                  *
 *                                                                           *
 *****************************************************************************/

Bool XShmQueryExtension (Display *dpy /*  int *event_basep, *error_basep */)
{
    XExtDisplayInfo *info = find_display (dpy);

    if (XextHasExtension(info)) {
/*	*event_basep = info->codes->first_event;
	*error_basep = info->codes->error_event; */
	return True;
    } else {
	return False;
    }
}


int XShmGetEventBase(Display *dpy)
{
    XExtDisplayInfo *info = find_display (dpy);

    if (XextHasExtension(info)) {
	return info->codes->first_event;
    } else {
	return -1;
    }
}


Bool XShmQueryVersion(
    Display *dpy,
    int *majorVersion,
    int *minorVersion,
    Bool *sharedPixmaps)
{
    XExtDisplayInfo *info = find_display (dpy);
    xShmQueryVersionReply rep;
    register xShmQueryVersionReq *req;

    ShmCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(ShmQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->shmReqType = X_ShmQueryVersion;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }
    *majorVersion = rep.majorVersion;
    *minorVersion = rep.minorVersion;
    *sharedPixmaps = rep.sharedPixmaps ? True : False;
    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}


int XShmPixmapFormat(Display *dpy)
{
    XExtDisplayInfo *info = find_display (dpy);
    xShmQueryVersionReply rep;
    register xShmQueryVersionReq *req;

    ShmCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(ShmQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->shmReqType = X_ShmQueryVersion;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return 0;
    }
    UnlockDisplay(dpy);
    SyncHandle();
    if (rep.sharedPixmaps &&
	(rep.majorVersion > 1 || rep.minorVersion > 0))
	return rep.pixmapFormat;
    return 0;
}


Bool XShmAttach(Display *dpy, XShmSegmentInfo *shminfo)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShmAttachReq *req;

    ShmCheckExtension (dpy, info, 0);

    LockDisplay(dpy);
    GetReq(ShmAttach, req);
    req->reqType = info->codes->major_opcode;
    req->shmReqType = X_ShmAttach;
    req->shmseg = shminfo->shmseg = XAllocID(dpy);
    req->shmid = shminfo->shmid;
    req->readOnly = shminfo->readOnly ? xTrue : xFalse;
    UnlockDisplay(dpy);
    SyncHandle();
    return 1;
}


Bool XShmDetach(Display *dpy, XShmSegmentInfo *shminfo)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xShmDetachReq *req;

    ShmCheckExtension (dpy, info, 0);

    LockDisplay(dpy);
    GetReq(ShmDetach, req);
    req->reqType = info->codes->major_opcode;
    req->shmReqType = X_ShmDetach;
    req->shmseg = shminfo->shmseg;
    UnlockDisplay(dpy);
    SyncHandle();
    return 1;
}

static int _XShmDestroyImage (XImage *ximage)
{
	Xfree(ximage);
	return 1;
}

#define ROUNDUP(nbytes, pad) ((((nbytes) + ((pad) - 1)) / (pad)) * (pad))

XImage *XShmCreateImage (
    register Display *dpy,
    register Visual *visual,
    unsigned int depth,
    int format,
    char *data,
    XShmSegmentInfo *shminfo,
    unsigned int width,
    unsigned int height)
{
    register XImage *image;

    image = Xcalloc(1, sizeof(XImage));
    if (!image)
	return image;
    image->data = data;
    image->obdata = (char *)shminfo;
    image->width = width;
    image->height = height;
    image->depth = depth;
    image->format = format;
    image->byte_order = dpy->byte_order;
    image->bitmap_unit = dpy->bitmap_unit;
    image->bitmap_bit_order = dpy->bitmap_bit_order;
    image->bitmap_pad = _XGetScanlinePad(dpy, depth);
    image->xoffset = 0;
    if (visual) {
	image->red_mask = visual->red_mask;
	image->green_mask = visual->green_mask;
	image->blue_mask = visual->blue_mask;
    } else {
	image->red_mask = image->green_mask = image->blue_mask = 0;
    }
    if (format == ZPixmap)
	image->bits_per_pixel = _XGetBitsPerPixel(dpy, (int)depth);
    else
	image->bits_per_pixel = 1;
    image->bytes_per_line = ROUNDUP((image->bits_per_pixel * width),
				    image->bitmap_pad) >> 3;
    _XInitImageFuncPtrs(image);
    image->f.destroy_image = _XShmDestroyImage;
    return image;
}

Bool XShmPutImage (
    register Display *dpy,
    Drawable d,
    GC gc,
    register XImage *image,
    int src_x, int src_y, int dst_x, int dst_y,
    unsigned int src_width, unsigned int src_height,
    Bool send_event)
{
    XExtDisplayInfo *info = find_display (dpy);
    XShmSegmentInfo *shminfo = (XShmSegmentInfo *)image->obdata;
    register xShmPutImageReq *req;

    ShmCheckExtension (dpy, info, 0);
    if (!shminfo) return 0;

    LockDisplay(dpy);
    FlushGC(dpy, gc);
    GetReq(ShmPutImage, req);
    req->reqType = info->codes->major_opcode;
    req->shmReqType = X_ShmPutImage;
    req->drawable = d;
    req->gc = gc->gid;
    req->srcX = src_x;
    req->srcY = src_y;
    req->srcWidth = src_width;
    req->srcHeight = src_height;
    req->dstX = dst_x;
    req->dstY = dst_y;
    req->totalWidth = image->width;
    req->totalHeight = image->height;
    req->depth = image->depth;
    req->format = image->format;
    req->sendEvent = send_event;
    req->shmseg = shminfo->shmseg;
    req->offset = image->data - shminfo->shmaddr;
    UnlockDisplay(dpy);
    SyncHandle();
    return 1;
}


Bool XShmGetImage(
    register Display *dpy,
    Drawable d,
    XImage *image,
    int x, int y,
    unsigned long plane_mask)
{
    XExtDisplayInfo *info = find_display (dpy);
    XShmSegmentInfo *shminfo = (XShmSegmentInfo *)image->obdata;
    register xShmGetImageReq *req;
    xShmGetImageReply rep;
    register Visual *visual;

    ShmCheckExtension (dpy, info, 0);
    if (!shminfo) return 0;

    LockDisplay(dpy);
    GetReq(ShmGetImage, req);
    req->reqType = info->codes->major_opcode;
    req->shmReqType = X_ShmGetImage;
    req->drawable = d;
    req->x = x;
    req->y = y;
    req->width = image->width;
    req->height = image->height;
    req->planeMask = plane_mask;
    req->format = image->format;
    req->shmseg = shminfo->shmseg;
    req->offset = image->data - shminfo->shmaddr;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return 0;
    }
    visual = _XVIDtoVisual(dpy, rep.visual);
    if (visual) {
    	image->red_mask = visual->red_mask;
    	image->green_mask = visual->green_mask;
    	image->blue_mask = visual->blue_mask;
    } else {
	image->red_mask = image->green_mask = image->blue_mask = 0;
    }
    UnlockDisplay(dpy);
    SyncHandle();
    return 1;
}

Pixmap XShmCreatePixmap (
    register Display *dpy,
    Drawable d,
    char *data,
    XShmSegmentInfo *shminfo,
    unsigned int width, unsigned int height, unsigned int depth)
{
    XExtDisplayInfo *info = find_display (dpy);
    Pixmap pid;
    register xShmCreatePixmapReq *req;

    ShmCheckExtension (dpy, info, 0);

    LockDisplay(dpy);
    GetReq(ShmCreatePixmap, req);
    req->reqType = info->codes->major_opcode;
    req->shmReqType = X_ShmCreatePixmap;
    req->drawable = d;
    req->width = width;
    req->height = height;
    req->depth = depth;
    req->shmseg = shminfo->shmseg;
    req->offset = data - shminfo->shmaddr;
    pid = req->pid = XAllocID(dpy);
    UnlockDisplay(dpy);
    SyncHandle();
    return pid;
}
