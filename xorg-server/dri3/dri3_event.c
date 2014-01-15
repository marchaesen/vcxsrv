/*
 * Copyright © 2013 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "dri3_priv.h"

RESTYPE dri3_event_type;

static int
dri3_free_event(void *data, XID id)
{
    dri3_event_ptr dri3_event = (dri3_event_ptr) data;
    dri3_window_priv_ptr window_priv = dri3_window_priv(dri3_event->window);
    dri3_event_ptr *previous, current;

    for (previous = &window_priv->events; (current = *previous); previous = &current->next) {
        if (current == dri3_event) {
            *previous = dri3_event->next;
            break;
        }
    }
    free((void *) dri3_event);
    return 1;

}

void
dri3_free_events(WindowPtr window)
{
    dri3_window_priv_ptr window_priv = dri3_window_priv(window);
    dri3_event_ptr event;

    if (!window_priv)
        return;

    while ((event = window_priv->events))
        FreeResource(event->id, RT_NONE);
}

static void
dri3_event_swap(xGenericEvent *from, xGenericEvent *to)
{
    *to = *from;
    swaps(&to->sequenceNumber);
    swapl(&to->length);
    swaps(&to->evtype);
    switch (from->evtype) {
    case DRI3_ConfigureNotify: {
        xDRI3ConfigureNotify *c = (xDRI3ConfigureNotify *) to;

        swapl(&c->eid);
        swapl(&c->window);
        swaps(&c->x);
        swaps(&c->y);
        swaps(&c->width);
        swaps(&c->height);
        swaps(&c->off_x);
        swaps(&c->off_y);
        swaps(&c->pixmap_width);
        swaps(&c->pixmap_height);
        swapl(&c->pixmap_flags);
        break;
    }
    }
}

void
dri3_send_config_notify(WindowPtr window, int x, int y, int w, int h, int bw, WindowPtr sibling)
{
    dri3_window_priv_ptr window_priv = dri3_window_priv(window);

    if (window_priv) {
        xDRI3ConfigureNotify cn = {
            .type = GenericEvent,
            .extension = dri3_request,
            .length = (sizeof(xDRI3ConfigureNotify) - 32) >> 2,
            .evtype = DRI3_ConfigureNotify,
            .eid = 0,
            .window = window->drawable.id,
            .x = x,
            .y = y,
            .width = w,
            .height = h,
            .off_x = 0,
            .off_y = 0,
            .pixmap_width = w,
            .pixmap_height = h,
            .pixmap_flags = 0
        };
        dri3_event_ptr event;
        dri3_screen_priv_ptr screen_priv = dri3_screen_priv(window->drawable.pScreen);

        if (screen_priv->info && screen_priv->info->driver_config)
            screen_priv->info->driver_config(window, &cn);

        for (event = window_priv->events; event; event = event->next) {
            if (event->mask & (1 << DRI3ConfigureNotify)) {
                cn.eid = event->id;
                WriteEventsToClient(event->client, 1, (xEvent *) &cn);
            }
        }
    }
}

int
dri3_select_input(ClientPtr client, XID eid, WindowPtr window, CARD32 mask)
{
    dri3_window_priv_ptr window_priv = dri3_window_priv(window);
    dri3_event_ptr event;

    if (!window_priv)
        return BadAlloc;

    event = calloc (1, sizeof (dri3_event_rec));
    if (!event)
        return BadAlloc;

    event->client = client;
    event->window = window;
    event->id = eid;
    event->mask = mask;

    event->next = window_priv->events;
    window_priv->events = event;

    if (!AddResource(event->id, dri3_event_type, (void *) event))
        return BadAlloc;

    return Success;
}

Bool
dri3_event_init(void)
{
    dri3_event_type = CreateNewResourceType(dri3_free_event, "DRI3Event");
    if (!dri3_event_type)
        return FALSE;

    GERegisterExtension(dri3_request, dri3_event_swap);
    return TRUE;
}
