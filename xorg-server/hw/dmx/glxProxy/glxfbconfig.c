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

#include "glxfbconfig.h"

int
AreFBConfigsMatch(__GLXFBConfig * c1, __GLXFBConfig * c2)
{
    int match;

    match = ((c1->visualType == c2->visualType) &&
             (c1->transparentType == c2->transparentType) &&
             (c1->transparentRed == c2->transparentRed) &&
             (c1->transparentGreen == c2->transparentGreen) &&
             (c1->transparentBlue == c2->transparentBlue) &&
             (c1->transparentAlpha == c2->transparentAlpha) &&
             (c1->transparentIndex == c2->transparentIndex) &&
             (c1->visualCaveat == c2->visualCaveat) &&
             (c1->drawableType == c2->drawableType) &&
             (c1->renderType == c2->renderType) &&
#if 0
             (c1->maxPbufferWidth == c2->maxPbufferWidth) &&
             (c1->maxPbufferHeight == c2->maxPbufferHeight) &&
             (c1->maxPbufferPixels == c2->maxPbufferPixels) &&
             (c1->optimalPbufferWidth == c2->optimalPbufferWidth) &&
             (c1->optimalPbufferHeight == c2->optimalPbufferHeight) &&
#endif
             (c1->visualSelectGroup == c2->visualSelectGroup) &&
             (c1->rgbMode == c2->rgbMode) &&
             (c1->colorIndexMode == c2->colorIndexMode) &&
             (c1->doubleBufferMode == c2->doubleBufferMode) &&
             (c1->stereoMode == c2->stereoMode) &&
             (c1->haveAccumBuffer == c2->haveAccumBuffer) &&
             (c1->haveDepthBuffer == c2->haveDepthBuffer) &&
             (c1->haveStencilBuffer == c2->haveStencilBuffer) &&
             (c1->accumRedBits == c2->accumRedBits) &&
             (c1->accumGreenBits == c2->accumGreenBits) &&
             (c1->accumBlueBits == c2->accumBlueBits) &&
             (c1->accumAlphaBits == c2->accumAlphaBits) &&
             (c1->depthBits == c2->depthBits) &&
             (c1->stencilBits == c2->stencilBits) &&
             (c1->indexBits == c2->indexBits) &&
             (c1->redBits == c2->redBits) &&
             (c1->greenBits == c2->greenBits) &&
             (c1->blueBits == c2->blueBits) &&
             (c1->alphaBits == c2->alphaBits) &&
             (c1->redMask == c2->redMask) &&
             (c1->greenMask == c2->greenMask) &&
             (c1->blueMask == c2->blueMask) &&
             (c1->alphaMask == c2->alphaMask) &&
             (c1->multiSampleSize == c2->multiSampleSize) &&
             (c1->nMultiSampleBuffers == c2->nMultiSampleBuffers) &&
             (c1->maxAuxBuffers == c2->maxAuxBuffers) &&
             (c1->level == c2->level) &&
             (c1->extendedRange == c2->extendedRange) &&
             (c1->minRed == c2->minRed) &&
             (c1->maxRed == c2->maxRed) &&
             (c1->minGreen == c2->minGreen) &&
             (c1->maxGreen == c2->maxGreen) &&
             (c1->minBlue == c2->minBlue) &&
             (c1->maxBlue == c2->maxBlue) &&
             (c1->minAlpha == c2->minAlpha) && (c1->maxAlpha == c2->maxAlpha)
        );

    return match;
}

__GLXFBConfig *
FindMatchingFBConfig(__GLXFBConfig * c, __GLXFBConfig * configs, int nconfigs)
{
    int i;

    for (i = 0; i < nconfigs; i++) {
        if (AreFBConfigsMatch(c, configs + i))
            return configs + i;
    }

    return 0;
}
