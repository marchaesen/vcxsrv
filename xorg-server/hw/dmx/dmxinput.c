/*
 * Copyright 2001,2002 Red Hat Inc., Durham, North Carolina.
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
 *   David H. Dawes <dawes@xfree86.org>
 *   Kevin E. Martin <kem@redhat.com>
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Provide the main entry points for input initialization and processing
 * that arequired by the dix layer.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxlog.h"
#include "dmxinput.h"

#include "inputstr.h"
#include "input.h"
#include "mi.h"

/** Returns TRUE if the key is a valid modifier.  For PC-class
 * keyboards, all keys can be used as modifiers, so return TRUE
 * always. */
Bool
LegalModifier(unsigned int key, DeviceIntPtr pDev)
{
    return TRUE;
}

/** Called from dix/main.c on each server generation to initialize
 * inputs.  All the work is done in dmxInputInit.  \see
 * dmxInputInit() */
void
InitInput(int argc, char **argv)
{
    int i;
    DMXInputInfo *dmxInput;

    if (!dmxNumInputs)
        dmxLog(dmxFatal, "InitInput: no inputs specified\n");

    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++)
        dmxInputInit(dmxInput);

    mieqInit();
}

void
CloseInput(void)
{
    mieqFini();
}

/** Called from dix/dispatch.c in Dispatch() whenever input events
 * require processing.  All the work is done in the lower level
 * routines. */
void
ProcessInputEvents(void)
{
    int i;
    DMXInputInfo *dmxInput;

    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++)
        if (!dmxInput->detached && dmxInput->processInputEvents)
            dmxInput->processInputEvents(dmxInput);
}

/** This routine is called from \a dmxwindow.c whenever the layout of
 * windows on the display might have changed.  This information is used
 * by input drivers (currently only the console driver) that provide
 * information about window layout to the user. */
void
dmxUpdateWindowInfo(DMXUpdateType type, WindowPtr pWindow)
{
    int i;
    DMXInputInfo *dmxInput;

    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++)
        if (!dmxInput->detached && dmxInput->updateWindowInfo)
            dmxInput->updateWindowInfo(dmxInput, type, pWindow);
}

int
NewInputDeviceRequest(InputOption *options, InputAttributes * attrs,
                      DeviceIntPtr *pdev)
{
    return BadRequest;
}

void
DeleteInputDeviceRequest(DeviceIntPtr pDev)
{
}
