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
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * Interface for gcops support.  \see dmxgcops.c */

#ifndef DMXGCOPS_H
#define DMXGCOPS_H

extern void dmxFillSpans(DrawablePtr pDrawable, GCPtr pGC,
                         int nInit, DDXPointPtr pptInit, int *pwidthInit,
                         int fSorted);
extern void dmxSetSpans(DrawablePtr pDrawable, GCPtr pGC,
                        char *psrc, DDXPointPtr ppt, int *pwidth, int nspans,
                        int fSorted);
extern void dmxPutImage(DrawablePtr pDrawable, GCPtr pGC,
                        int depth, int x, int y, int w, int h,
                        int leftPad, int format, char *pBits);
extern RegionPtr dmxCopyArea(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                             int srcx, int srcy, int w, int h,
                             int dstx, int dsty);
extern RegionPtr dmxCopyPlane(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                              int srcx, int srcy, int width, int height,
                              int dstx, int dsty, unsigned long bitPlane);
extern void dmxPolyPoint(DrawablePtr pDrawable, GCPtr pGC,
                         int mode, int npt, DDXPointPtr pptInit);
extern void dmxPolylines(DrawablePtr pDrawable, GCPtr pGC,
                         int mode, int npt, DDXPointPtr pptInit);
extern void dmxPolySegment(DrawablePtr pDrawable, GCPtr pGC,
                           int nseg, xSegment * pSegs);
extern void dmxPolyRectangle(DrawablePtr pDrawable, GCPtr pGC,
                             int nrects, xRectangle *pRects);
extern void dmxPolyArc(DrawablePtr pDrawable, GCPtr pGC,
                       int narcs, xArc * parcs);
extern void dmxFillPolygon(DrawablePtr pDrawable, GCPtr pGC,
                           int shape, int mode, int count, DDXPointPtr pPts);
extern void dmxPolyFillRect(DrawablePtr pDrawable, GCPtr pGC,
                            int nrectFill, xRectangle *prectInit);
extern void dmxPolyFillArc(DrawablePtr pDrawable, GCPtr pGC,
                           int narcs, xArc * parcs);
extern int dmxPolyText8(DrawablePtr pDrawable, GCPtr pGC,
                        int x, int y, int count, char *chars);
extern int dmxPolyText16(DrawablePtr pDrawable, GCPtr pGC,
                         int x, int y, int count, unsigned short *chars);
extern void dmxImageText8(DrawablePtr pDrawable, GCPtr pGC,
                          int x, int y, int count, char *chars);
extern void dmxImageText16(DrawablePtr pDrawable, GCPtr pGC,
                           int x, int y, int count, unsigned short *chars);
extern void dmxImageGlyphBlt(DrawablePtr pDrawable, GCPtr pGC,
                             int x, int y, unsigned int nglyph,
                             CharInfoPtr * ppci, void *pglyphBase);
extern void dmxPolyGlyphBlt(DrawablePtr pDrawable, GCPtr pGC,
                            int x, int y, unsigned int nglyph,
                            CharInfoPtr * ppci, void *pglyphBase);
extern void dmxPushPixels(GCPtr pGC, PixmapPtr pBitMap, DrawablePtr pDst,
                          int w, int h, int x, int y);

extern void dmxGetImage(DrawablePtr pDrawable, int sx, int sy, int w, int h,
                        unsigned int format, unsigned long planeMask,
                        char *pdstLine);
extern void dmxGetSpans(DrawablePtr pDrawable, int wMax,
                        DDXPointPtr ppt, int *pwidth, int nspans,
                        char *pdstStart);

#endif                          /* DMXGCOPS_H */
