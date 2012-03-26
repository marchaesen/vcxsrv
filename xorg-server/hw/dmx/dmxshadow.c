/*
 * Copyright 2001 Red Hat Inc., Durham, North Carolina.
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
 *   Kevin E. Martin <kem@redhat.com>
 *   David H. Dawes <dawes@xfree86.org>
 *
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxsync.h"
#include "dmxshadow.h"

/** \file
 * This file provides support for the shadow frame buffer. */

/** Update the screen from the shadow frame buffer. */
void
dmxShadowUpdateProc(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    RegionPtr damage = &pBuf->damage;
    int nbox = RegionNumRects(damage);
    BoxPtr pbox = RegionRects(damage);
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];

    if (!dmxScreen->beDisplay)
        return;

    while (nbox--) {
        XPutImage(dmxScreen->beDisplay,
                  dmxScreen->scrnWin,
                  dmxScreen->shadowGC,
                  dmxScreen->shadowFBImage,
                  pbox->x1, pbox->y1,
                  pbox->x1, pbox->y1, pbox->x2 - pbox->x1, pbox->y2 - pbox->y1);

        pbox++;
    }

    dmxSync(dmxScreen, FALSE);
}
