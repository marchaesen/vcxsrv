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
 *
 * This code implements a low-level device driver for a USB mouse. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "usb-private.h"

/*****************************************************************************/
/* Define some macros to make it easier to move this file to another
 * part of the Xserver tree.  All calls to the dmx* layer are #defined
 * here for the .c file.  The .h file will also have to be edited. */
#include "usb-mouse.h"

#define GETPRIV       myPrivate *priv                            \
                      = ((DMXLocalInputInfoPtr)(pDev->devicePrivate))->private

#define LOG0(f)       dmxLog(dmxDebug,f)
#define LOG1(f,a)     dmxLog(dmxDebug,f,a)
#define LOG2(f,a,b)   dmxLog(dmxDebug,f,a,b)
#define LOG3(f,a,b,c) dmxLog(dmxDebug,f,a,b,c)
#define FATAL0(f)     dmxLog(dmxFatal,f)
#define FATAL1(f,a)   dmxLog(dmxFatal,f,a)
#define FATAL2(f,a,b) dmxLog(dmxFatal,f,a,b)
#define MOTIONPROC    dmxMotionProcPtr
#define ENQUEUEPROC   dmxEnqueueProcPtr
#define CHECKPROC     dmxCheckSpecialProcPtr
#define BLOCK         DMXBlockType

/* End of interface definitions. */
/*****************************************************************************/

/** Read the USB device using #usbRead. */
void
mouUSBRead(DevicePtr pDev,
           MOTIONPROC motion,
           ENQUEUEPROC enqueue, CHECKPROC checkspecial, BLOCK block)
{
    usbRead(pDev, motion, enqueue, BTN_MISC, block);
}

/** Initialize \a pDev using #usbInit. */
void
mouUSBInit(DevicePtr pDev)
{
    usbInit(pDev, usbMouse);
}

/** Turn \a pDev on (i.e., take input from \a pDev). */
int
mouUSBOn(DevicePtr pDev)
{
    GETPRIV;

    if (priv->fd < 0)
        mouUSBInit(pDev);
    return priv->fd;
}

static void
mouUSBGetMap(DevicePtr pDev, unsigned char *map, int *nButtons)
{
    int i;

    if (nButtons)
        *nButtons = 5;
    if (map)
        for (i = 0; i <= *nButtons; i++)
            map[i] = i;
}

/** Fill the \a info structure with information needed to initialize \a
 * pDev. */
void
mouUSBGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    static KeySym keyboard_mapping = NoSymbol;

    info->buttonClass = 1;
    mouUSBGetMap(pDev, info->map, &info->numButtons);
    info->valuatorClass = 1;
    info->numRelAxes = 2;
    info->minval[0] = 0;
    info->maxval[0] = 0;
    info->res[0] = 1;
    info->minres[0] = 0;
    info->maxres[0] = 1;
    info->ptrFeedbackClass = 1;

    /* Some USB mice devices return key
     * events from their pair'd
     * keyboard...  */
    info->keyClass = 1;
    info->keySyms.minKeyCode = 8;
    info->keySyms.maxKeyCode = 8;
    info->keySyms.mapWidth = 1;
    info->keySyms.map = &keyboard_mapping;
}
