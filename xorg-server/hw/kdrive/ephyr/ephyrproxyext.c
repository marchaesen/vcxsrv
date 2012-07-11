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

/*
 * \file
 * This file defines a proxy extension that forwards requests.
 * When a request to extension FOO is sent to Xephyr, that request is forwared
 * to the host X, without even trying to know what the request means.
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "misc.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "ephyrproxyext.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include "ephyrhostproxy.h"
#include "hostx.h"

static Bool ephyrProxyGetHostExtensionInfo(const char *a_ext_name,
                                           int *a_major_opcode,
                                           int *a_first_event,
                                           int *a_first_error);

static int ephyrProxyProcDispatch(ClientPtr client);

static Bool
ephyrProxyGetHostExtensionInfo(const char *a_ext_name,
                               int *a_major_opcode,
                               int *a_first_event, int *a_first_error)
{
    return hostx_get_extension_info(a_ext_name, a_major_opcode,
                                    a_first_event, a_first_error);
}

static int
ephyrProxyProcDispatch(ClientPtr a_client)
{
    int res = BadImplementation;
    struct XReply reply;

    if (!ephyrHostProxyDoForward(a_client->requestBuffer, &reply, FALSE)) {
        EPHYR_LOG_ERROR("forwarding failed\n");
        goto out;
    }
    reply.sequence_number = a_client->sequence;
    res = Success;

    WriteToClient(a_client, 32, &reply);

 out:
    return res;
}

static void
ephyrProxyProcReset(ExtensionEntry * a_entry)
{
}

Bool
ephyrProxyExtensionInit(const char *a_extension_name)
{
    Bool is_ok = FALSE;
    int major_opcode = 0, first_event = 0, first_error = 0;
    ExtensionEntry *ext = NULL;

    if (!ephyrProxyGetHostExtensionInfo(a_extension_name,
                                        &major_opcode,
                                        &first_event, &first_error)) {
        EPHYR_LOG("failed to query extension %s from host\n", a_extension_name);
        goto out;
    }
    ext = AddExtension((char *) a_extension_name, 0, 0,
                       ephyrProxyProcDispatch,
                       ephyrProxyProcDispatch,
                       ephyrProxyProcReset, StandardMinorOpcode);
    if (!ext) {
        EPHYR_LOG_ERROR("failed to add the extension\n");
        goto out;
    }
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}
