/*
 * Copyright 2002-2004 Red Hat Inc., Durham, North Carolina.
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
 *
 */

/** \file
 * This file provides support for visuals. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxvisual.h"

#include "scrnintstr.h"

#ifdef GLXEXT

#include <GL/glxint.h>

extern VisualID glxMatchVisualInConfigList(ScreenPtr pScreen,
                                           VisualPtr pVisual,
                                           __GLXvisualConfig * configs,
                                           int nconfigs);

static Visual *
dmxLookupGLXVisual(ScreenPtr pScreen, VisualPtr pVisual)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    int j;
    VisualID vid;

    vid = glxMatchVisualInConfigList(pScreen, pVisual,
                                     dmxScreen->glxVisuals,
                                     dmxScreen->numGlxVisuals);
    if (vid) {
        /* Find the X visual of the matching GLX visual */
        for (j = 0; j < dmxScreen->beNumVisuals; j++)
            if (vid == dmxScreen->beVisuals[j].visualid)
                return dmxScreen->beVisuals[j].visual;
    }

    /* No matching visual found */
    return NULL;
}
#endif

/** Return the visual that matched \a pVisual. */
Visual *
dmxLookupVisual(ScreenPtr pScreen, VisualPtr pVisual)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    int i;

#ifdef GLXEXT
    Visual *retval;
#endif

    if (!dmxScreen->beDisplay)
        return NULL;

#ifdef GLXEXT
    if ((retval = dmxLookupGLXVisual(pScreen, pVisual)))
        return retval;
#endif

    for (i = 0; i < dmxScreen->beNumVisuals; i++) {
        if (pVisual->class == dmxScreen->beVisuals[i].class &&
            pVisual->bitsPerRGBValue == dmxScreen->beVisuals[i].bits_per_rgb &&
            pVisual->ColormapEntries == dmxScreen->beVisuals[i].colormap_size &&
            pVisual->nplanes == dmxScreen->beVisuals[i].depth &&
            pVisual->redMask == dmxScreen->beVisuals[i].red_mask &&
            pVisual->greenMask == dmxScreen->beVisuals[i].green_mask &&
            pVisual->blueMask == dmxScreen->beVisuals[i].blue_mask) {
            return dmxScreen->beVisuals[i].visual;
        }
    }

    return NULL;
}

/** Return the visual that matched the \a vid. */
Visual *
dmxLookupVisualFromID(ScreenPtr pScreen, VisualID vid)
{
    Visual *visual;
    int i;

    if (!dmxScreens[pScreen->myNum].beDisplay)
        return NULL;

    for (i = 0; i < pScreen->numVisuals; i++) {
        if (pScreen->visuals[i].vid == vid) {
            visual = dmxLookupVisual(pScreen, &pScreen->visuals[i]);
            if (visual)
                return visual;
        }
    }

    return NULL;
}

/** Return the colormap for the \a visual. */
Colormap
dmxColormapFromDefaultVisual(ScreenPtr pScreen, Visual * visual)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    int i;

    if (dmxScreen->beDisplay) {
        for (i = 0; i < dmxScreen->beNumDefColormaps; i++)
            if (visual == dmxScreen->beVisuals[i].visual)
                return dmxScreen->beDefColormaps[i];
    }

    return None;
}
