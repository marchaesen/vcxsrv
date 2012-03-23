/*
 * SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
 * Copyright (C) 1991-2000 Silicon Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice including the dates of first publication and
 * either this permission notice or a reference to
 * http://oss.sgi.com/projects/FreeB/
 * shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * SILICON GRAPHICS, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of Silicon Graphics, Inc.
 * shall not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization from
 * Silicon Graphics, Inc.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "glxserver.h"
#include "glxvisuals.h"

int
glxVisualsMatch(__GLXvisualConfig * v1, __GLXvisualConfig * v2)
{
    if ((v1->class == v2->class) &&
        (v1->rgba == v2->rgba) &&
        (v1->redSize == v2->redSize) &&
        (v1->greenSize == v2->greenSize) &&
        (v1->blueSize == v2->blueSize) &&
        (v1->alphaSize == v2->alphaSize) &&
        (v1->redMask == v2->redMask) &&
        (v1->greenMask == v2->greenMask) &&
        (v1->blueMask == v2->blueMask) &&
        (v1->alphaMask == v2->alphaMask) &&
        (v1->accumRedSize == v2->accumRedSize) &&
        (v1->accumGreenSize == v2->accumGreenSize) &&
        (v1->accumBlueSize == v2->accumBlueSize) &&
        (v1->accumAlphaSize == v2->accumAlphaSize) &&
        (v1->doubleBuffer == v2->doubleBuffer) &&
        (v1->stereo == v2->stereo) &&
        (v1->bufferSize == v2->bufferSize) &&
        (v1->depthSize == v2->depthSize) &&
        (v1->stencilSize == v2->stencilSize) &&
        (v1->auxBuffers == v2->auxBuffers) &&
        (v1->level == v2->level) &&
        (v1->visualRating == v2->visualRating) &&
        (v1->transparentPixel == v2->transparentPixel) &&
        (v1->transparentRed == v2->transparentRed) &&
        (v1->transparentGreen == v2->transparentGreen) &&
        (v1->transparentBlue == v2->transparentBlue) &&
        (v1->transparentAlpha == v2->transparentAlpha) &&
        (v1->transparentIndex == v2->transparentIndex) &&
        (v1->multiSampleSize == v2->multiSampleSize) &&
        (v1->nMultiSampleBuffers == v2->nMultiSampleBuffers) &&
        (v1->visualSelectGroup == v2->visualSelectGroup)) {

        return 1;

    }

    return 0;

}

VisualID
glxMatchGLXVisualInConfigList(__GLXvisualConfig * pGlxVisual,
                              __GLXvisualConfig * configs, int nconfigs)
{
    int i;

    for (i = 0; i < nconfigs; i++) {

        if (glxVisualsMatch(pGlxVisual, &configs[i])) {

            return configs[i].vid;

        }
    }

    return 0;
}

VisualID
glxMatchVisualInConfigList(ScreenPtr pScreen, VisualPtr pVisual,
                           __GLXvisualConfig * configs, int nconfigs)
{
    __GLXscreenInfo *pGlxScreen;
    __GLXvisualConfig *pGlxVisual;
    int i;

    /* check that the glx extension has been initialized */
    if (!__glXActiveScreens)
        return 0;

    pGlxScreen = &__glXActiveScreens[pScreen->myNum];
    pGlxVisual = pGlxScreen->pGlxVisual;

    /* find the glx visual info for pVisual */
    for (i = 0; i < pGlxScreen->numVisuals; i++, pGlxVisual++) {
        if (pGlxVisual->vid == pVisual->vid) {
            break;
        }
    }
    if (i == pGlxScreen->numVisuals) {
        /*
         * the visual is not supported by glx
         */
        return 0;
    }

    return (glxMatchGLXVisualInConfigList(pGlxVisual, configs, nconfigs));
}

VisualPtr
glxMatchVisual(ScreenPtr pScreen, VisualPtr pVisual, ScreenPtr pMatchScreen)
{
    __GLXscreenInfo *pGlxScreen2;
    int j;
    VisualID vid;

    /* check that the glx extension has been initialized */
    if (!__glXActiveScreens)
        return NULL;

    pGlxScreen2 = &__glXActiveScreens[pMatchScreen->myNum];

    vid = glxMatchVisualInConfigList(pScreen, pVisual,
                                     pGlxScreen2->pGlxVisual,
                                     pGlxScreen2->numVisuals);
    if (vid) {
        /*
         * find the X visual of the matching glx visual
         */
        for (j = 0; j < pMatchScreen->numVisuals; j++) {
            if (vid == pMatchScreen->visuals[j].vid) {
                return &pMatchScreen->visuals[j];
            }
        }
    }

    return 0;
}
