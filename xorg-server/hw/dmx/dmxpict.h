/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
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
 *  This file provides access to the externally visible RENDER support
 *  functions, global variables and macros for DMX.
 *  
 *  FIXME: Move function definitions for non-externally visible function
 *  to .c file. */

#ifndef DMXPICT_H
#define DMXPICT_H

/** Picture private structure */
typedef struct _dmxPictPriv {
    Picture pict;               /**< Picture ID from back-end server */
    Mask savedMask;             /**< Mask of picture attributes saved for
				 *   lazy window creation. */
} dmxPictPrivRec, *dmxPictPrivPtr;

/** Glyph Set private structure */
typedef struct _dmxGlyphPriv {
    GlyphSet *glyphSets;  /**< Glyph Set IDs from back-end server */
} dmxGlyphPrivRec, *dmxGlyphPrivPtr;

extern void dmxInitRender(void);
extern void dmxResetRender(void);

extern Bool dmxPictureInit(ScreenPtr pScreen,
                           PictFormatPtr formats, int nformats);

extern void dmxCreatePictureList(WindowPtr pWindow);
extern Bool dmxDestroyPictureList(WindowPtr pWindow);

extern int dmxCreatePicture(PicturePtr pPicture);
extern void dmxDestroyPicture(PicturePtr pPicture);
extern int dmxChangePictureClip(PicturePtr pPicture, int clipType,
                                pointer value, int n);
extern void dmxDestroyPictureClip(PicturePtr pPicture);
extern void dmxChangePicture(PicturePtr pPicture, Mask mask);
extern void dmxValidatePicture(PicturePtr pPicture, Mask mask);
extern void dmxComposite(CARD8 op,
                         PicturePtr pSrc, PicturePtr pMask, PicturePtr pDst,
                         INT16 xSrc, INT16 ySrc,
                         INT16 xMask, INT16 yMask,
                         INT16 xDst, INT16 yDst, CARD16 width, CARD16 height);
extern void dmxGlyphs(CARD8 op,
                      PicturePtr pSrc, PicturePtr pDst,
                      PictFormatPtr maskFormat,
                      INT16 xSrc, INT16 ySrc,
                      int nlists, GlyphListPtr lists, GlyphPtr * glyphs);
extern void dmxCompositeRects(CARD8 op,
                              PicturePtr pDst,
                              xRenderColor * color,
                              int nRect, xRectangle *rects);
extern Bool dmxInitIndexed(ScreenPtr pScreen, PictFormatPtr pFormat);
extern void dmxCloseIndexed(ScreenPtr pScreen, PictFormatPtr pFormat);
extern void dmxUpdateIndexed(ScreenPtr pScreen, PictFormatPtr pFormat,
                             int ndef, xColorItem * pdef);
extern void dmxTrapezoids(CARD8 op,
                          PicturePtr pSrc, PicturePtr pDst,
                          PictFormatPtr maskFormat,
                          INT16 xSrc, INT16 ySrc,
                          int ntrap, xTrapezoid * traps);
extern void dmxTriangles(CARD8 op,
                         PicturePtr pSrc, PicturePtr pDst,
                         PictFormatPtr maskFormat,
                         INT16 xSrc, INT16 ySrc, int ntri, xTriangle * tris);

extern int dmxBECreateGlyphSet(int idx, GlyphSetPtr glyphSet);
extern Bool dmxBEFreeGlyphSet(ScreenPtr pScreen, GlyphSetPtr glyphSet);
extern int dmxBECreatePicture(PicturePtr pPicture);
extern Bool dmxBEFreePicture(PicturePtr pPicture);

/** Get the picture private data given a picture pointer */
#define DMX_GET_PICT_PRIV(_pPict)					\
    (dmxPictPrivPtr)dixLookupPrivate(&(_pPict)->devPrivates, dmxPictPrivateKey)

/** Set the glyphset private data given a glyphset pointer */
#define DMX_SET_GLYPH_PRIV(_pGlyph, _pPriv)				\
    GlyphSetSetPrivate((_pGlyph), dmxGlyphSetPrivateKey, (_pPriv))
/** Get the glyphset private data given a glyphset pointer */
#define DMX_GET_GLYPH_PRIV(_pGlyph)					\
    (dmxGlyphPrivPtr)GlyphSetGetPrivate((_pGlyph), dmxGlyphSetPrivateKey)

#endif                          /* DMXPICT_H */
