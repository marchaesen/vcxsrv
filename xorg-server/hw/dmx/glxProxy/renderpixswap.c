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

#include "glxserver.h"
#include "unpack.h"
#include "g_disptab.h"

void
__glXDispSwap_PolygonStipple(GLbyte * pc)
{
    __GLXpixelHeader *hdr = (__GLXpixelHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_Bitmap(GLbyte * pc)
{
    __GLXdispatchBitmapHeader *hdr = (__GLXdispatchBitmapHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_FLOAT((GLbyte *) &hdr->xorig);
    __GLX_SWAP_FLOAT((GLbyte *) &hdr->yorig);
    __GLX_SWAP_FLOAT((GLbyte *) &hdr->xmove);
    __GLX_SWAP_FLOAT((GLbyte *) &hdr->ymove);

    hdr->swapBytes = !hdr->swapBytes;

}

void
__glXDispSwap_TexImage1D(GLbyte * pc)
{
    __GLXdispatchTexImageHeader *hdr = (__GLXdispatchTexImageHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->level);
    __GLX_SWAP_INT((GLbyte *) &hdr->components);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->border);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_TexImage2D(GLbyte * pc)
{
    __GLXdispatchTexImageHeader *hdr = (__GLXdispatchTexImageHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->level);
    __GLX_SWAP_INT((GLbyte *) &hdr->components);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->border);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_TexImage3D(GLbyte * pc)
{
    __GLXdispatchTexImage3DHeader *hdr = (__GLXdispatchTexImage3DHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->imageHeight);
    __GLX_SWAP_INT((GLbyte *) &hdr->imageDepth);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipImages);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipVolumes);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->level);
    __GLX_SWAP_INT((GLbyte *) &hdr->internalformat);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->depth);
    __GLX_SWAP_INT((GLbyte *) &hdr->size4d);
    __GLX_SWAP_INT((GLbyte *) &hdr->border);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_DrawPixels(GLbyte * pc)
{
    __GLXdispatchDrawPixelsHeader *hdr = (__GLXdispatchDrawPixelsHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_TexSubImage1D(GLbyte * pc)
{
    __GLXdispatchTexSubImageHeader *hdr = (__GLXdispatchTexSubImageHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->level);
    __GLX_SWAP_INT((GLbyte *) &hdr->xoffset);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_TexSubImage2D(GLbyte * pc)
{
    __GLXdispatchTexSubImageHeader *hdr = (__GLXdispatchTexSubImageHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->level);
    __GLX_SWAP_INT((GLbyte *) &hdr->xoffset);
    __GLX_SWAP_INT((GLbyte *) &hdr->yoffset);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_TexSubImage3D(GLbyte * pc)
{
    __GLXdispatchTexSubImage3DHeader *hdr =
        (__GLXdispatchTexSubImage3DHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->imageHeight);
    __GLX_SWAP_INT((GLbyte *) &hdr->imageDepth);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipImages);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipVolumes);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->level);
    __GLX_SWAP_INT((GLbyte *) &hdr->xoffset);
    __GLX_SWAP_INT((GLbyte *) &hdr->yoffset);
    __GLX_SWAP_INT((GLbyte *) &hdr->zoffset);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->depth);
    __GLX_SWAP_INT((GLbyte *) &hdr->size4d);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_ColorTable(GLbyte * pc)
{
    __GLXdispatchColorTableHeader *hdr = (__GLXdispatchColorTableHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->internalformat);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_ColorSubTable(GLbyte * pc)
{
    __GLXdispatchColorSubTableHeader *hdr =
        (__GLXdispatchColorSubTableHeader *) pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->start);
    __GLX_SWAP_INT((GLbyte *) &hdr->count);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_ConvolutionFilter1D(GLbyte * pc)
{
    __GLXdispatchConvolutionFilterHeader *hdr =
        (__GLXdispatchConvolutionFilterHeader *) pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->internalformat);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_ConvolutionFilter2D(GLbyte * pc)
{
    __GLXdispatchConvolutionFilterHeader *hdr =
        (__GLXdispatchConvolutionFilterHeader *) pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->internalformat);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}

void
__glXDispSwap_SeparableFilter2D(GLbyte * pc)
{
    __GLXdispatchConvolutionFilterHeader *hdr =
        (__GLXdispatchConvolutionFilterHeader *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT((GLbyte *) &hdr->rowLength);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipRows);
    __GLX_SWAP_INT((GLbyte *) &hdr->skipPixels);
    __GLX_SWAP_INT((GLbyte *) &hdr->alignment);

    __GLX_SWAP_INT((GLbyte *) &hdr->target);
    __GLX_SWAP_INT((GLbyte *) &hdr->internalformat);
    __GLX_SWAP_INT((GLbyte *) &hdr->width);
    __GLX_SWAP_INT((GLbyte *) &hdr->height);
    __GLX_SWAP_INT((GLbyte *) &hdr->format);
    __GLX_SWAP_INT((GLbyte *) &hdr->type);

    /*
     ** Just invert swapBytes flag; the GL will figure out if it needs to swap
     ** the pixel data.
     */
    hdr->swapBytes = !hdr->swapBytes;
}
