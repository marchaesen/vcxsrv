/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
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
 *
 */

/** \file
 * Provide mouse and keyboard that are sufficient for starting the X
 * server, but that don't actually provide any events.  This is useful
 * for testing. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxinputinit.h"
#include "dmxdummy.h"

/** Return information about the dummy keyboard device specified in \a pDev
 * into the structure pointed to by \a info.  The keyboard is set up to
 * have 1 valid key code that is \a NoSymbol */
void
dmxDummyKbdGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    static KeySym keyboard_mapping = NoSymbol;

    info->keyboard = 1;
    info->keyClass = 1;
    info->keySyms.minKeyCode = 8;
    info->keySyms.maxKeyCode = 8;
    info->keySyms.mapWidth = 1;
    info->keySyms.map = &keyboard_mapping;
    info->freemap = 0;
    info->focusClass = 1;
    info->kbdFeedbackClass = 1;
    info->force = 1;
}

/** Return information about the dummy mouse device specified in \a pDev
 * into the structure pointed to by \a info.  They mouse has 3 buttons
 * and two axes. */
void
dmxDummyMouGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    info->buttonClass = 1;
    info->numButtons = 3;
    info->map[0] = 1;
    info->map[1] = 2;
    info->map[2] = 3;
    info->valuatorClass = 1;
    info->numRelAxes = 2;
    info->minval[0] = 0;
    info->minval[1] = 0;
    info->maxval[0] = 0;
    info->maxval[1] = 0;
    info->res[0] = 1;
    info->minres[0] = 0;
    info->maxres[0] = 1;
    info->ptrFeedbackClass = 1;
}
