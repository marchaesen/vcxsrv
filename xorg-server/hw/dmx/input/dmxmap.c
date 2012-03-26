/*
 * Copyright 2003 Red Hat Inc., Durham, North Carolina.
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

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 */

/** \file
 *
 * This file implements a mapping from remote XInput event types to Xdmx
 * XInput event types.
 *
 * The exglobals.h file defines global server-side variables with names
 * Device* to be integers that hold the value of the type of the
 * server-side XInput extension event.
 *
 * The client-side X11/extensions/XInput.h file defines macros with THE
 * EXACT SAME Device* names!
 *
 * Using those macros to extract remote server event type values from
 * the (opaque) XDevice structure is appropriate, but makes a direct
 * mapping to the Device* integers impossible.  So we use the normalized
 * XI_Device* names for these routines.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmxinputinit.h"
#include "dmxmap.h"

/** Create a mapping from \a remoteEvent to \a serverEvent. The \a
 * remoteEvent is the type returned from the remote server.  The \a
 * serverEvent is from the XI_* list of events in
 * include/extensions/XIproto.h. */
void
dmxMapInsert(DMXLocalInputInfoPtr dmxLocal, int remoteEvent, int serverEvent)
{
    int hash = remoteEvent & DMX_MAP_MASK;
    int i;

    /* Return if this has already been mapped */
    if (dmxLocal->map[hash].remote == remoteEvent
        && dmxLocal->map[hash].server == serverEvent)
        return;

    if (dmxLocal->map[hash].remote) {
        dmxLocal->mapOptimize = 0;
        for (i = 0; i < DMX_MAP_ENTRIES; i++) {
            if (!dmxLocal->map[i].remote) {
                dmxLocal->map[i].remote = remoteEvent;
                dmxLocal->map[i].server = serverEvent;
                return;
            }
        }
        dmxLog(dmxWarning,
               "Out of map entries, cannot map remove event type %d\n",
               remoteEvent);
    }
    else {
        dmxLocal->map[hash].remote = remoteEvent;
        dmxLocal->map[hash].server = serverEvent;
    }
}

/** Remove all mappings there were inserted with #dmxMapInsert. */
void
dmxMapClear(DMXLocalInputInfoPtr dmxLocal)
{
    int i;

    for (i = 0; i < DMX_MAP_ENTRIES; i++)
        dmxLocal->map[i].remote = 0;
    dmxLocal->mapOptimize = 1;
}

/** Lookup a mapping for \a remoteEvent.  The \a remoteEvent is the type
 * returned from the remote server.  The return value is that which was
 * passed into #dmxMapInsert (i.e., a value from the XI_* list in
 * include/extensions/XIproto.h).  If a mapping is not available, -1 is
 * returned. */
int
dmxMapLookup(DMXLocalInputInfoPtr dmxLocal, int remoteEvent)
{
    int hash = remoteEvent & DMX_MAP_MASK;
    int serverEvent = -1;
    int i;

    if (dmxLocal->mapOptimize && dmxLocal->map[hash].remote == remoteEvent) {
        serverEvent = dmxLocal->map[hash].server;
    }
    else {
        for (i = 0; i < DMX_MAP_ENTRIES; i++)
            if (dmxLocal->map[i].remote == remoteEvent) {
                serverEvent = dmxLocal->map[hash].server;
                break;
            }
    }

    return serverEvent;
}
