/*
 * Copyright 1985, 1987, 1998  The Open Group
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of The Open Group shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from The Open Group.
 */
/* $XFree86 */

/*
 * Copyright 2002-2003 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/** \file
 * This file provides a #XCheckNotMaskEvent function that is derived
 * from the standard Xlib XCheckMaskEvent function. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "ChkNotMaskEv.h"

extern long const _Xevent_to_mask[];

/** Check existing events in queue to find if any match.  If so, return.
 * If not, flush buffer and see if any more events are readable. If one
 * matches, return.  If all else fails, tell the user no events found.
 */
Bool
XCheckNotMaskEvent(Display * dpy, long mask, XEvent * event)
{
    register _XQEvent *prev, *qelt;
    unsigned long qe_serial = 0;
    int n;                      /* time through count */

    LockDisplay(dpy);
    prev = NULL;
    for (n = 3; --n >= 0;) {
        for (qelt = prev ? prev->next : dpy->head;
             qelt; prev = qelt, qelt = qelt->next) {
            if (qelt->event.type >= LASTEvent
                || !(_Xevent_to_mask[qelt->event.type] & mask)) {
                *event = qelt->event;
                _XDeq(dpy, prev, qelt);
                UnlockDisplay(dpy);
                return True;
            }
        }
        if (prev)
            qe_serial = prev->qserial_num;
        switch (n) {
        case 2:
            _XEventsQueued(dpy, QueuedAfterReading);
            break;
        case 1:
            _XFlush(dpy);
            break;
        }
        if (prev && prev->qserial_num != qe_serial)
            /* another thread has snatched this event */
            prev = NULL;
    }
    UnlockDisplay(dpy);
    return False;
}
