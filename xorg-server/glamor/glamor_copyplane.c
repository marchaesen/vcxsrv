/*
 * Copyright © 2009 Intel Corporation
 * Copyright © 1998 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Zhigang Gong <zhigang.gong@gmail.com>
 *
 */

#include "glamor_priv.h"

static Bool
_glamor_copy_plane(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                   int srcx, int srcy, int w, int h, int dstx, int dsty,
                   unsigned long bitPlane, RegionPtr *pRegion, Bool fallback)
{
    if (!fallback && glamor_ddx_fallback_check_gc(pGC)
        && glamor_ddx_fallback_check_pixmap(pSrc)
        && glamor_ddx_fallback_check_pixmap(pDst))
        goto fail;

    if (glamor_prepare_access(pDst, GLAMOR_ACCESS_RW) &&
        glamor_prepare_access(pSrc, GLAMOR_ACCESS_RO) &&
        glamor_prepare_access_gc(pGC)) {
        *pRegion = fbCopyPlane(pSrc, pDst, pGC, srcx, srcy, w, h,
                               dstx, dsty, bitPlane);
    }
    glamor_finish_access_gc(pGC);
    glamor_finish_access(pSrc);
    glamor_finish_access(pDst);
    return TRUE;

 fail:
    return FALSE;
}

RegionPtr
glamor_copy_plane(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                  int srcx, int srcy, int w, int h, int dstx, int dsty,
                  unsigned long bitPlane)
{
    RegionPtr ret;

    _glamor_copy_plane(pSrc, pDst, pGC, srcx, srcy, w, h,
                       dstx, dsty, bitPlane, &ret, TRUE);
    return ret;
}

Bool
glamor_copy_plane_nf(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                     int srcx, int srcy, int w, int h, int dstx, int dsty,
                     unsigned long bitPlane, RegionPtr *pRegion)
{
    return _glamor_copy_plane(pSrc, pDst, pGC, srcx, srcy, w, h,
                              dstx, dsty, bitPlane, pRegion, FALSE);
}
