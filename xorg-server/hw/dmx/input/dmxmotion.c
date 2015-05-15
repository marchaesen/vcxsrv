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

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * This file provides functions similar to miPointerGetMotionEvents and
 * miPointerPutMotionEvents, with the exception that devices with more
 * than two axes are fully supported.  These routines may be used only
 * for motion buffers for extension devices, and are \a not compatible
 * replacements for the mi routines.  */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "inputstr.h"
#include "dmxinputinit.h"
#include "dmxcommon.h"
#include "dmxmotion.h"

#define OFFSET(offset,element) ((offset) * (numAxes + 1) + (element))

/** Return size of motion buffer. \see DMX_MOTION_SIZE */
int
dmxPointerGetMotionBufferSize(void)
{
    return DMX_MOTION_SIZE;
}

/** This routine performs the same function as \a miPointerGetMotionEvents:
 * the events in the motion history that are between the start and stop
 * times (in mS) are placed in the coords vector, and the count of the
 * number of items so placed is returned.  This routine is called from
 * dix/devices.c so that coords can hold valuator->numMotionEvents
 * events.  This routine is called from \a Xi/gtmotion.c with coords large
 * enough to hold the same number of events in a variable-length
 * extended \a xTimecoord structure.  This provides sufficient data for the
 * \a XGetDeviceMotionEvents library call, and would be identical to
 * \a miPointerGetMotionEvents for devices with only 2 axes (i.e., core
 * pointers) if \a xTimecoord used 32bit integers.
 *
 * Because DMX uses the mi* routines for all core devices, this routine
 * only has to support extension devices using the polymorphic coords.
 * Because compatibility with miPointerGetMotionEvents is not possible,
 * it is not provided. */
int
dmxPointerGetMotionEvents(DeviceIntPtr pDevice,
                          xTimecoord * coords,
                          unsigned long start,
                          unsigned long stop, ScreenPtr pScreen)
{
    GETDMXLOCALFROMPDEVICE;
    int numAxes = pDevice->valuator->numAxes;
    unsigned long *c = (unsigned long *) coords;
    int count = 0;
    int i, j;

    if (!dmxLocal->history)
        return 0;
    for (i = dmxLocal->head; i != dmxLocal->tail;) {
        if (dmxLocal->history[OFFSET(i, 0)] >= stop)
            break;
        if (dmxLocal->history[OFFSET(i, 0)] >= start) {
            for (j = 0; j < numAxes + 1; j++)
                c[OFFSET(count, j)] = dmxLocal->history[OFFSET(i, j)];
            ++count;
        }
        if (++i >= DMX_MOTION_SIZE)
            i = 0;
    }
    return count;
}

/** This routine adds an event to the motion history.  A similar
 * function is performed by miPointerMove for the mi versions of these
 * routines. */
void
dmxPointerPutMotionEvent(DeviceIntPtr pDevice,
                         int firstAxis, int axesCount, int *v,
                         unsigned long time)
{
    GETDMXLOCALFROMPDEVICE;
    int numAxes = pDevice->valuator->numAxes;
    int i;

    if (!dmxLocal->history) {
        dmxLocal->history = xallocarray(numAxes + 1,
                                 sizeof(*dmxLocal->history) * DMX_MOTION_SIZE);
        dmxLocal->head = 0;
        dmxLocal->tail = 0;
        dmxLocal->valuators = calloc(sizeof(*dmxLocal->valuators), numAxes);
    }
    else {
        if (++dmxLocal->tail >= DMX_MOTION_SIZE)
            dmxLocal->tail = 0;
        if (dmxLocal->head == dmxLocal->tail)
            if (++dmxLocal->head >= DMX_MOTION_SIZE)
                dmxLocal->head = 0;
    }

    dmxLocal->history[OFFSET(dmxLocal->tail, 0)] = time;

    /* Initialize the data from the known
     * values (if Absolute) or to zero (if
     * Relative) */
    for (i = 0; i < numAxes; i++) {
        if (pDevice->valuator->axes[i].mode == Absolute)
            dmxLocal->history[OFFSET(dmxLocal->tail, i + 1)]
                = dmxLocal->valuators[i];
        else
            dmxLocal->history[OFFSET(dmxLocal->tail, i + 1)] = 0;
    }

    for (i = firstAxis; i < axesCount; i++) {
        dmxLocal->history[OFFSET(dmxLocal->tail, i + i)]
            = (unsigned long) v[i - firstAxis];
        dmxLocal->valuators[i] = v[i - firstAxis];
    }
}
