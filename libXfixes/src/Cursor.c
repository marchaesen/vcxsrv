/*
 * Copyright (c) 2006, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Copyright © 2002 Keith Packard, member of The XFree86 Project, Inc.
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

void
XFixesSelectCursorInput (Display	*dpy,
			 Window		win,
			 unsigned long	eventMask)
{
    XFixesExtDisplayInfo	    *info = XFixesFindDisplay (dpy);
    xXFixesSelectCursorInputReq	    *req;

    XFixesSimpleCheckExtension (dpy, info);

    LockDisplay (dpy);
    GetReq (XFixesSelectCursorInput, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSelectCursorInput;
    req->window = win;
    req->eventMask = eventMask;
    UnlockDisplay (dpy);
    SyncHandle ();
}

XFixesCursorImage *
XFixesGetCursorImage (Display *dpy)
{
    XFixesExtDisplayInfo		*info = XFixesFindDisplay (dpy);
    xXFixesGetCursorImageAndNameReq	*req;
    xXFixesGetCursorImageAndNameReply	rep;
    int					npixels;
    int					nbytes_name;
    int					nbytes, nread, rlength;
    XFixesCursorImage			*image;
    char				*name;

    XFixesCheckExtension (dpy, info, NULL);
    LockDisplay (dpy);
    GetReq (XFixesGetCursorImageAndName, req);
    req->reqType = info->codes->major_opcode;
    if (info->major_version >= 2)
	req->xfixesReqType = X_XFixesGetCursorImageAndName;
    else
	req->xfixesReqType = X_XFixesGetCursorImage;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse))
    {
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }
    if (info->major_version < 2)
    {
	rep.cursorName = None;
	rep.nbytes = 0;
    }
    npixels = rep.width * rep.height;
    nbytes_name = rep.nbytes;
    /* reply data length */
    nbytes = (long) rep.length << 2;
    /* bytes of actual data in the reply */
    nread = (npixels << 2) + nbytes_name;
    /* size of data returned to application */
    rlength = (sizeof (XFixesCursorImage) + 
	       npixels * sizeof (unsigned long) +
	       nbytes_name + 1);

    image = (XFixesCursorImage *) Xmalloc (rlength);
    if (!image)
    {
	_XEatData (dpy, nbytes);
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }
    image->x = rep.x;
    image->y = rep.y;
    image->width = rep.width;
    image->height = rep.height;
    image->xhot = rep.xhot;
    image->yhot = rep.yhot;
    image->cursor_serial = rep.cursorSerial;
    image->pixels = (unsigned long *) (image + 1);
    image->atom = rep.cursorName;
    name = (char *) (image->pixels + npixels);
    image->name = name;
    _XRead32 (dpy, (long *) image->pixels, npixels << 2);
    _XRead (dpy, name, nbytes_name);
    name[nbytes_name] = '\0';	/* null-terminate */
    /* skip any padding */
    if(nbytes > nread)
    {
	_XEatData (dpy, (unsigned long) (nbytes - nread));
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    return image;
}

void
XFixesSetCursorName (Display *dpy, Cursor cursor, const char *name)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesSetCursorNameReq	*req;
    int				nbytes = strlen (name);

    XFixesSimpleCheckExtension (dpy, info);
    if (info->major_version < 2)
	return;
    LockDisplay (dpy);
    GetReq (XFixesSetCursorName, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesSetCursorName;
    req->cursor = cursor;
    req->nbytes = nbytes;
    req->length += (nbytes + 3) >> 2;
    Data (dpy, name, nbytes);
    UnlockDisplay (dpy);
    SyncHandle ();
}

const char *
XFixesGetCursorName (Display *dpy, Cursor cursor, Atom *atom)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesGetCursorNameReq	*req;
    xXFixesGetCursorNameReply	rep;
    char			*name;

    XFixesCheckExtension (dpy, info, NULL);
    if (info->major_version < 2)
	return NULL;
    LockDisplay (dpy);
    GetReq (XFixesGetCursorName, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesGetCursorName;
    req->cursor = cursor;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse))
    {
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }
    *atom = rep.atom;
    if ((name = (char *) Xmalloc(rep.nbytes+1))) {
	_XReadPad(dpy, name, (long)rep.nbytes);
	name[rep.nbytes] = '\0';
    } else {
	_XEatData(dpy, (unsigned long) (rep.nbytes + 3) & ~3);
	name = (char *) NULL;
    }
    UnlockDisplay(dpy);
    SyncHandle();
    return(name);
}

void
XFixesChangeCursor (Display *dpy, Cursor source, Cursor destination)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesChangeCursorReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    if (info->major_version < 2)
	return;
    LockDisplay (dpy);
    GetReq (XFixesChangeCursor, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesChangeCursor;
    req->source = source;
    req->destination = destination;
    UnlockDisplay(dpy);
    SyncHandle();
}

void
XFixesChangeCursorByName (Display *dpy, Cursor source, const char *name)
{
    XFixesExtDisplayInfo	    *info = XFixesFindDisplay (dpy);
    xXFixesChangeCursorByNameReq    *req;
    int				    nbytes = strlen (name);

    XFixesSimpleCheckExtension (dpy, info);
    if (info->major_version < 2)
	return;
    LockDisplay (dpy);
    GetReq (XFixesChangeCursorByName, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesChangeCursorByName;
    req->source = source;
    req->nbytes = nbytes;
    req->length += (nbytes + 3) >> 2;
    Data (dpy, name, nbytes);
    UnlockDisplay(dpy);
    SyncHandle();
}

void
XFixesHideCursor (Display *dpy, Window win)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesHideCursorReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    if (info->major_version < 4)
	return;
    LockDisplay (dpy);
    GetReq (XFixesHideCursor, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesHideCursor;
    req->window = win;
    UnlockDisplay (dpy);
    SyncHandle ();
}

void
XFixesShowCursor (Display *dpy, Window win)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);
    xXFixesShowCursorReq	*req;

    XFixesSimpleCheckExtension (dpy, info);
    if (info->major_version < 4)
	return;
    LockDisplay (dpy);
    GetReq (XFixesShowCursor, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesShowCursor;
    req->window = win;
    UnlockDisplay (dpy);
    SyncHandle ();
}

PointerBarrier
XFixesCreatePointerBarrier(Display *dpy, Window w, int x1, int y1,
			   int x2, int y2, int directions,
			   int num_devices, int *devices)
{
    XFixesExtDisplayInfo *info = XFixesFindDisplay (dpy);
    xXFixesCreatePointerBarrierReq *req;
    PointerBarrier barrier;
    int extra = 0;

    XFixesCheckExtension (dpy, info, 0);
    if (info->major_version < 5)
	return 0;

    if (num_devices)
	extra = (((2 * num_devices) + 3) / 4) * 4;

    LockDisplay (dpy);
    GetReqExtra (XFixesCreatePointerBarrier, extra, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesCreatePointerBarrier;
    barrier = req->barrier = XAllocID (dpy);
    req->window = w;
    req->x1 = x1;
    req->y1 = y1;
    req->x2 = x2;
    req->y2 = y2;
    req->directions = directions;
    if ((req->num_devices = num_devices)) {
	int i;
	CARD16 *devs = (CARD16 *)(req + 1);
	for (i = 0; i < num_devices; i++)
	    devs[i] = (CARD16)(devices[i]);
    }

    UnlockDisplay (dpy);
    SyncHandle();
    return barrier;
}

void
XFixesDestroyPointerBarrier(Display *dpy, PointerBarrier b)
{
    XFixesExtDisplayInfo *info = XFixesFindDisplay (dpy);
    xXFixesDestroyPointerBarrierReq *req;

    XFixesSimpleCheckExtension (dpy, info);
    if (info->major_version < 5)
	return;

    LockDisplay (dpy);
    GetReq (XFixesDestroyPointerBarrier, req);
    req->reqType = info->codes->major_opcode;
    req->xfixesReqType = X_XFixesDestroyPointerBarrier;
    req->barrier = b;
    UnlockDisplay (dpy);
    SyncHandle();
}
