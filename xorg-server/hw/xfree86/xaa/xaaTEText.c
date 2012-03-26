
/********************************************************************

   In this file we have GC level replacements for PolyText8/16,
   ImageText8/16, ImageGlyphBlt and PolyGlyphBlt for TE (fixed) fonts.
   The idea is that everything in this file is device independent.
   The mentioned GCOps are merely wrappers for XAAGlyphBltTEColorExpansion
   which calculates the boxes containing arbitrarily clipped text
   and passes them to the TEGlyphRenderer which will usually be a lower 
   level XAA function which renders these clipped glyphs using
   the basic color expansion functions exported by the chipset driver.
   The TEGlyphRenderer itself may optionally be driver supplied to
   facilitate work-arounds/optimizations at a higher level than usual.

   v1.0 - Mark Vojkovich (mvojkovi@ucsd.edu)

********************************************************************/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include <X11/fonts/font.h>
#include "scrnintstr.h"
#include "dixfontstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "gcstruct.h"
#include "pixmapstr.h"

static void XAAGlyphBltTEColorExpansion(ScrnInfoPtr pScrn, int xInit,
                                        int yInit, FontPtr font, int fg, int bg,
                                        int rop, unsigned int planemask,
                                        RegionPtr cclip, int nglyph,
                                        unsigned char *gBase,
                                        CharInfoPtr * ppci);

/********************************************************************

   GC level replacements for PolyText8/16 and ImageText8/16
   for TE fonts when using color expansion.

********************************************************************/

int
XAAPolyText8TEColorExpansion(DrawablePtr pDraw,
                             GCPtr pGC, int x, int y, int count, char *chars)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    unsigned long n;

    (*pGC->font->get_glyphs) (pGC->font, (unsigned long) count,
                              (unsigned char *) chars, Linear8Bit, &n,
                              infoRec->CharInfo);

    /* we have divorced XAAGlyphBltTEColorExpansion from the drawable */
    if (n)
        XAAGlyphBltTEColorExpansion(infoRec->pScrn, x + pDraw->x, y + pDraw->y,
                                    pGC->font, pGC->fgPixel, -1, pGC->alu,
                                    pGC->planemask, pGC->pCompositeClip, n,
                                    FONTGLYPHS(pGC->font), infoRec->CharInfo);

    return (x + (n * FONTMAXBOUNDS(pGC->font, characterWidth)));
}

int
XAAPolyText16TEColorExpansion(DrawablePtr pDraw,
                              GCPtr pGC,
                              int x, int y, int count, unsigned short *chars)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    unsigned long n;

    (*pGC->font->get_glyphs) (pGC->font, (unsigned long) count,
                              (unsigned char *) chars,
                              (FONTLASTROW(pGC->font) ==
                               0) ? Linear16Bit : TwoD16Bit, &n,
                              infoRec->CharInfo);

    if (n)
        XAAGlyphBltTEColorExpansion(infoRec->pScrn, x + pDraw->x, y + pDraw->y,
                                    pGC->font, pGC->fgPixel, -1, pGC->alu,
                                    pGC->planemask, pGC->pCompositeClip, n,
                                    FONTGLYPHS(pGC->font), infoRec->CharInfo);

    return (x + (n * FONTMAXBOUNDS(pGC->font, characterWidth)));
}

void
XAAImageText8TEColorExpansion(DrawablePtr pDraw,
                              GCPtr pGC, int x, int y, int count, char *chars)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    unsigned long n;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    (*pGC->font->get_glyphs) (pGC->font, (unsigned long) count,
                              (unsigned char *) chars, Linear8Bit, &n,
                              infoRec->CharInfo);

    if (n)
        XAAGlyphBltTEColorExpansion(infoRec->pScrn, x + pDraw->x, y + pDraw->y,
                                    pGC->font, pGC->fgPixel, pGC->bgPixel,
                                    GXcopy, pGC->planemask, pGC->pCompositeClip,
                                    n, FONTGLYPHS(pGC->font),
                                    infoRec->CharInfo);
}

void
XAAImageText16TEColorExpansion(DrawablePtr pDraw,
                               GCPtr pGC,
                               int x, int y, int count, unsigned short *chars)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    unsigned long n;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    (*pGC->font->get_glyphs) (pGC->font, (unsigned long) count,
                              (unsigned char *) chars,
                              (FONTLASTROW(pGC->font) ==
                               0) ? Linear16Bit : TwoD16Bit, &n,
                              infoRec->CharInfo);

    if (n)
        XAAGlyphBltTEColorExpansion(infoRec->pScrn, x + pDraw->x, y + pDraw->y,
                                    pGC->font, pGC->fgPixel, pGC->bgPixel,
                                    GXcopy, pGC->planemask, pGC->pCompositeClip,
                                    n, FONTGLYPHS(pGC->font),
                                    infoRec->CharInfo);
}

/********************************************************************

   GC level replacements for ImageGlyphBlt and PolyGlyphBlt for
   TE fonts when using color expansion.

********************************************************************/

void
XAAImageGlyphBltTEColorExpansion(DrawablePtr pDrawable,
                                 GCPtr pGC,
                                 int xInit, int yInit,
                                 unsigned int nglyph,
                                 CharInfoPtr * ppci, pointer pglyphBase)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    XAAGlyphBltTEColorExpansion(infoRec->pScrn, xInit + pDrawable->x,
                                yInit + pDrawable->y, pGC->font, pGC->fgPixel,
                                pGC->bgPixel, GXcopy, pGC->planemask,
                                pGC->pCompositeClip, nglyph,
                                (unsigned char *) pglyphBase, ppci);
}

void
XAAPolyGlyphBltTEColorExpansion(DrawablePtr pDrawable,
                                GCPtr pGC,
                                int xInit, int yInit,
                                unsigned int nglyph,
                                CharInfoPtr * ppci, pointer pglyphBase)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    XAAGlyphBltTEColorExpansion(infoRec->pScrn, xInit + pDrawable->x,
                                yInit + pDrawable->y, pGC->font, pGC->fgPixel,
                                -1, pGC->alu, pGC->planemask,
                                pGC->pCompositeClip, nglyph,
                                (unsigned char *) pglyphBase, ppci);
}

/********************************************************************

   XAAGlyphBltTEColorExpansion -

   This guy computes the clipped pieces of text and sends it to
   the lower-level function which will handle acceleration of 
   arbitrarily clipped text.
  
********************************************************************/

static void
XAAGlyphBltTEColorExpansion(ScrnInfoPtr pScrn,
                            int xInit, int yInit,
                            FontPtr font,
                            int fg, int bg,
                            int rop,
                            unsigned int planemask,
                            RegionPtr cclip,
                            int nglyph,
                            unsigned char *gBase, CharInfoPtr * ppci)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int skippix, skipglyphs;
    int Left, Right, Top, Bottom;
    int LeftEdge, RightEdge, ytop, ybot;
    int nbox = RegionNumRects(cclip);
    BoxPtr pbox = RegionRects(cclip);
    unsigned int **glyphs = NULL;
    int glyphWidth = FONTMAXBOUNDS(font, characterWidth);

    /* find the size of the box */
    Left = xInit;
    Right = Left + (glyphWidth * nglyph);
    Top = yInit - FONTASCENT(font);
    Bottom = yInit + FONTDESCENT(font);

    /* get into the first band that may contain part of our string */
    while (nbox && (Top >= pbox->y2)) {
        pbox++;
        nbox--;
    }

    /* stop when the lower edge of the box is beyond our string */
    while (nbox && (Bottom > pbox->y1)) {
        LeftEdge = max(Left, pbox->x1);
        RightEdge = min(Right, pbox->x2);

        if (RightEdge > LeftEdge) {     /* we have something to draw */
            unsigned int *fallbackBits = NULL;

            ytop = max(Top, pbox->y1);
            ybot = min(Bottom, pbox->y2);

            if ((skippix = LeftEdge - Left)) {
                skipglyphs = skippix / glyphWidth;
                skippix %= glyphWidth;
            }
            else
                skipglyphs = 0;

            if (!glyphs) {
                int count;

                glyphs = (unsigned int **) (infoRec->PreAllocMem);

                for (count = 0; count < nglyph; count++) {
                    glyphs[count] = (unsigned int *)
                        FONTGLYPHBITS(gBase, *ppci++);
                    if (!glyphs[count]) {
                        /* Glyphs with NULL bits do exist in the wild.
                           Replace with blank bits in that case */

                        if (!fallbackBits) {
                            int fontHeight = Bottom - Top + 1;

                            fallbackBits = calloc(glyphWidth * fontHeight, 1);
                            if (!fallbackBits)
                                return;
                        }
                        glyphs[count] = fallbackBits;
                    }
                }

                /* our new unrolled TE code only writes DWORDS at a time 
                   so it can read up to 6 characters past the last one 
                   we're displaying */
                glyphs[count + 0] = glyphs[0];
                glyphs[count + 1] = glyphs[0];
                glyphs[count + 2] = glyphs[0];
                glyphs[count + 3] = glyphs[0];
                glyphs[count + 4] = glyphs[0];
                glyphs[count + 5] = glyphs[0];
            }

            /* x, y, w, h, skipleft, skiptop, glyphp, glyphWidth, fg, bg, rop, pm */

            (*infoRec->TEGlyphRenderer) (pScrn,
                                         LeftEdge, ytop, RightEdge - LeftEdge,
                                         ybot - ytop, skippix, ytop - Top,
                                         glyphs + skipglyphs, glyphWidth, fg,
                                         bg, rop, planemask);

            free(fallbackBits);
        }

        nbox--;
        pbox++;
    }
}
