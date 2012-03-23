/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2007 OpenedHand Ltd 
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *    Dodji Seketeli <dodji@openedhand.com>
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <X11/Xlibint.h>
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include "ephyrhostproxy.h"
#include "hostx.h"

/* byte swap a short */
#define swaps(x, n) { \
    n = ((char *) (x))[0];\
    ((char *) (x))[0] = ((char *) (x))[1];\
    ((char *) (x))[1] = n; }

#define GetXReq(req) \
    WORD64ALIGN ;\
    if ((dpy->bufptr + SIZEOF(xReq)) > dpy->bufmax)\
            _XFlush(dpy);\
    req = (xReq *)(dpy->last_req = dpy->bufptr);\
    dpy->bufptr += SIZEOF(xReq);\
    dpy->request++

Bool
ephyrHostProxyDoForward(pointer a_request_buffer,
                        struct XReply *a_reply, Bool a_do_swap)
{
    Bool is_ok = FALSE;
    int n = 0;
    Display *dpy = hostx_get_display();
    xReq *in_req = (xReq *) a_request_buffer;
    xReq *forward_req = NULL;
    struct XReply reply;

    EPHYR_RETURN_VAL_IF_FAIL(in_req && dpy, FALSE);

    EPHYR_LOG("enter\n");

    if (a_do_swap) {
        swaps(&in_req->length);
    }
    EPHYR_LOG("Req {type:%d, data:%d, length:%d}\n",
              in_req->reqType, in_req->data, in_req->length);
    GetXReq(forward_req);
    memmove(forward_req, in_req, 4);

    if (!_XReply(dpy, (xReply *) & reply, 0, FALSE)) {
        EPHYR_LOG_ERROR("failed to get reply\n");
        goto out;
    }
    EPHYR_LOG("XReply{type:%d, foo:%d, seqnum:%d, length:%d}\n",
              reply.type, reply.foo, reply.sequence_number, reply.length);

    if (a_reply) {
        memmove(a_reply, &reply, sizeof(reply));
    }
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}
