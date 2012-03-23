
/********************************************************************

   In this file we have GC level replacements for PolyText8/16,
   ImageText8/16, ImageGlyphBlt and PolyGlyphBlt for NonTE (proportional) 
   fonts. The idea is that everything in this file is device independent.
   The mentioned GCOps are merely wrappers for the 
   PolyGlyphBltNonTEColorExpansion and ImageGlyphBltNonTEColorExpansion
   functions which calculate the boxes containing arbitrarily clipped 
   text and passes them to the NonTEGlyphRenderer which will usually 
   be a lower level XAA function which renders these clipped glyphs using
   the basic color expansion functions exported by the chipset driver.
   The NonTEGlyphRenderer itself may optionally be driver supplied to
   facilitate work-arounds/optimizations at a higher level than usual.

   Written by Mark Vojkovich (mvojkovi@ucsd.edu)

********************************************************************/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include <X11/fonts/font.h>
#include "scrnintstr.h"
#include "dixfontstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaacexp.h"
#include "xaalocal.h"
#include "gcstruct.h"
#include "pixmapstr.h"

static void ImageGlyphBltNonTEColorExpansion(ScrnInfoPtr pScrn,
                                             int xInit, int yInit, FontPtr font,
                                             int fg, int bg, unsigned planemask,
                                             RegionPtr cclip, int nglyph,
                                             unsigned char *gBase,
                                             CharInfoPtr * ppci);
static int PolyGlyphBltNonTEColorExpansion(ScrnInfoPtr pScrn, int xInit,
                                           int yInit, FontPtr font, int fg,
                                           int rop, unsigned planemask,
                                           RegionPtr cclip, int nglyph,
                                           unsigned char *gBase,
                                           CharInfoPtr * ppci);

/********************************************************************

   GC level replacements for PolyText8/16 and ImageText8/16
   for NonTE fonts when using color expansion.

********************************************************************/

int
XAAPolyText8NonTEColorExpansion(DrawablePtr pDraw,
                                GCPtr pGC, int x, int y, int count, char *chars)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    unsigned long n;
    int width = 0;

    (*pGC->font->get_glyphs) (pGC->font, (unsigned long) count,
                              (unsigned char *) chars, Linear8Bit, &n,
                              infoRec->CharInfo);

    if (n) {
        width = PolyGlyphBltNonTEColorExpansion(infoRec->pScrn,
                                                x + pDraw->x, y + pDraw->y,
                                                pGC->font, pGC->fgPixel,
                                                pGC->alu, pGC->planemask,
                                                pGC->pCompositeClip, n,
                                                FONTGLYPHS(pGC->font),
                                                infoRec->CharInfo);
    }

    return x + width;
}

int
XAAPolyText16NonTEColorExpansion(DrawablePtr pDraw,
                                 GCPtr pGC,
                                 int x, int y, int count, unsigned short *chars)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    unsigned long n;
    int width = 0;

    (*pGC->font->get_glyphs) (pGC->font, (unsigned long) count,
                              (unsigned char *) chars,
                              (FONTLASTROW(pGC->font) ==
                               0) ? Linear16Bit : TwoD16Bit, &n,
                              infoRec->CharInfo);

    if (n) {
        width = PolyGlyphBltNonTEColorExpansion(infoRec->pScrn,
                                                x + pDraw->x, y + pDraw->y,
                                                pGC->font, pGC->fgPixel,
                                                pGC->alu, pGC->planemask,
                                                pGC->pCompositeClip, n,
                                                FONTGLYPHS(pGC->font),
                                                infoRec->CharInfo);
    }

    return x + width;
}

void
XAAImageText8NonTEColorExpansion(DrawablePtr pDraw,
                                 GCPtr pGC,
                                 int x, int y, int count, char *chars)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    unsigned long n;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    (*pGC->font->get_glyphs) (pGC->font, (unsigned long) count,
                              (unsigned char *) chars, Linear8Bit, &n,
                              infoRec->CharInfo);

    if (n)
        ImageGlyphBltNonTEColorExpansion(infoRec->pScrn, x + pDraw->x,
                                         y + pDraw->y, pGC->font, pGC->fgPixel,
                                         pGC->bgPixel, pGC->planemask,
                                         pGC->pCompositeClip, n,
                                         FONTGLYPHS(pGC->font),
                                         infoRec->CharInfo);
}

void
XAAImageText16NonTEColorExpansion(DrawablePtr pDraw,
                                  GCPtr pGC,
                                  int x,
                                  int y, int count, unsigned short *chars)
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
        ImageGlyphBltNonTEColorExpansion(infoRec->pScrn, x + pDraw->x,
                                         y + pDraw->y, pGC->font, pGC->fgPixel,
                                         pGC->bgPixel, pGC->planemask,
                                         pGC->pCompositeClip, n,
                                         FONTGLYPHS(pGC->font),
                                         infoRec->CharInfo);
}

/********************************************************************

   GC level replacements for ImageGlyphBlt and PolyGlyphBlt for
   NonTE fonts when using color expansion.

********************************************************************/

void
XAAImageGlyphBltNonTEColorExpansion(DrawablePtr pDraw, GCPtr pGC, int xInit, int yInit, unsigned int nglyph, CharInfoPtr * ppci,        /* array of character info */
                                    pointer pglyphBase  /* start of array of glyphs */
    )
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    ImageGlyphBltNonTEColorExpansion(infoRec->pScrn, xInit + pDraw->x,
                                     yInit + pDraw->y, pGC->font, pGC->fgPixel,
                                     pGC->bgPixel, pGC->planemask,
                                     pGC->pCompositeClip, nglyph,
                                     (unsigned char *) pglyphBase, ppci);
}

void
XAAPolyGlyphBltNonTEColorExpansion(DrawablePtr pDraw, GCPtr pGC, int xInit, int yInit, unsigned int nglyph, CharInfoPtr * ppci, /* array of character info */
                                   pointer pglyphBase   /* start of array of glyphs */
    )
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    PolyGlyphBltNonTEColorExpansion(infoRec->pScrn, xInit + pDraw->x,
                                    yInit + pDraw->y, pGC->font, pGC->fgPixel,
                                    pGC->alu, pGC->planemask,
                                    pGC->pCompositeClip, nglyph,
                                    (unsigned char *) pglyphBase, ppci);
}

/********************************************************************

   ImageGlyphBltNonTEColorExpansion -
   PolyGlyphBltNonTEColorExpansion -

   These guys compute the clipped pieces of text and send it to
   the lower-level function which will handle acceleration of 
   arbitrarily clipped text.
  
********************************************************************/

static int
CollectCharacterInfo(NonTEGlyphPtr glyphs,
                     unsigned int nglyph, CharInfoPtr * ppci, FontPtr pfont)
{
    int i, w = 0;

    for (i = 0; i < nglyph; i++, ppci++, glyphs++) {
        glyphs->bits = (unsigned char *) ((*ppci)->bits);
        glyphs->start = w + (*ppci)->metrics.leftSideBearing;
        glyphs->end = w + (*ppci)->metrics.rightSideBearing;
        glyphs->yoff = (*ppci)->metrics.ascent;
        glyphs->height = glyphs->yoff + (*ppci)->metrics.descent;
        glyphs->srcwidth = PADGLYPHWIDTHBYTES(glyphs->end - glyphs->start);
        w += (*ppci)->metrics.characterWidth;
    }
    return w;
}

static void
PolyGlyphBltAsSingleBitmap(ScrnInfoPtr pScrn,
                           int nglyph,
                           FontPtr font,
                           int xInit,
                           int yInit,
                           int nbox,
                           BoxPtr pbox, int fg, int rop, unsigned planemask)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *block, *pntr, *bits;
    int pitch, topLine, botLine, top, bot, height;
    int Left, Right, Top, Bottom;
    int LeftEdge, RightEdge;
    int bitPitch, shift, size, i, skippix;
    NonTEGlyphPtr glyphs = infoRec->GlyphInfo;
    Bool extra;

    Left = xInit + infoRec->GlyphInfo[0].start;
    Right = xInit + infoRec->GlyphInfo[nglyph - 1].end;
    Top = yInit - FONTMAXBOUNDS(font, ascent);
    Bottom = yInit + FONTMAXBOUNDS(font, descent);

    /* get into the first band that may contain part of our string */
    while (nbox && (Top >= pbox->y2)) {
        pbox++;
        nbox--;
    }

    if (!nbox)
        return;

    pitch = (Right - Left + 31) >> 5;
    size = (pitch << 2) * (Bottom - Top);
    block = calloc(1, size);

    topLine = 10000;
    botLine = -10000;

    while (nglyph--) {
        top = -glyphs->yoff;
        bot = top + glyphs->height;
        if (top < topLine)
            topLine = top;
        if (bot > botLine)
            botLine = bot;
        skippix = glyphs->start - infoRec->GlyphInfo[0].start;
        bits = (CARD32 *) glyphs->bits;
        bitPitch = glyphs->srcwidth >> 2;
        pntr = block + ((FONTMAXBOUNDS(font, ascent) + top) * pitch) +
            (skippix >> 5);
        shift = skippix & 31;
        extra = ((shift + glyphs->end - glyphs->start) > 32);

        for (i = top; i < bot; i++) {
            *pntr |= SHIFT_L(*bits, shift);
            if (extra)
                *(pntr + 1) |= SHIFT_R(*bits, 32 - shift);
            pntr += pitch;
            bits += bitPitch;
        }

        glyphs++;
    }

    pntr = block + ((FONTMAXBOUNDS(font, ascent) + topLine) * pitch);

    Top = yInit + topLine;
    Bottom = yInit + botLine;

    while (nbox && (Top >= pbox->y2)) {
        pbox++;
        nbox--;
    }

    while (nbox && (Bottom > pbox->y1)) {
        LeftEdge = max(Left, pbox->x1);
        RightEdge = min(Right, pbox->x2);

        if (RightEdge > LeftEdge) {
            skippix = LeftEdge - Left;
            topLine = max(Top, pbox->y1);
            botLine = min(Bottom, pbox->y2);
            height = botLine - topLine;

            if (height > 0)
                (*infoRec->WriteBitmap) (pScrn, LeftEdge, topLine,
                                         RightEdge - LeftEdge, height,
                                         (unsigned char *) (pntr +
                                                            ((topLine -
                                                              Top) * pitch) +
                                                            (skippix >> 5)),
                                         pitch << 2, skippix & 31, fg, -1, rop,
                                         planemask);
        }

        nbox--;
        pbox++;
    }

    free(block);
}

static void
ImageGlyphBltNonTEColorExpansion(ScrnInfoPtr pScrn,
                                 int xInit, int yInit,
                                 FontPtr font,
                                 int fg, int bg,
                                 unsigned planemask,
                                 RegionPtr cclip,
                                 int nglyph,
                                 unsigned char *gBase, CharInfoPtr * ppci)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int skippix, skipglyph, width, n, i;
    int Left, Right, Top, Bottom;
    int LeftEdge, RightEdge, ytop, ybot;
    int nbox = RegionNumRects(cclip);
    BoxPtr pbox = RegionRects(cclip);
    Bool AlreadySetup = FALSE;

    width = CollectCharacterInfo(infoRec->GlyphInfo, nglyph, ppci, font);

    /* find our backing rectangle dimensions */
    Left = xInit;
    Right = Left + width;
    Top = yInit - FONTASCENT(font);
    Bottom = yInit + FONTDESCENT(font);

    /* get into the first band that may contain part of our box */
    while (nbox && (Top >= pbox->y2)) {
        pbox++;
        nbox--;
    }

    while (nbox && (Bottom >= pbox->y1)) {
        /* handle backing rect first */
        LeftEdge = max(Left, pbox->x1);
        RightEdge = min(Right, pbox->x2);
        if (RightEdge > LeftEdge) {
            ytop = max(Top, pbox->y1);
            ybot = min(Bottom, pbox->y2);

            if (ybot > ytop) {
                if (!AlreadySetup) {
                    (*infoRec->SetupForSolidFill) (pScrn, bg, GXcopy,
                                                   planemask);
                    AlreadySetup = TRUE;
                }
                (*infoRec->SubsequentSolidFillRect) (pScrn,
                                                     LeftEdge, ytop,
                                                     RightEdge - LeftEdge,
                                                     ybot - ytop);
            }
        }
        nbox--;
        pbox++;
    }

    nbox = RegionNumRects(cclip);
    pbox = RegionRects(cclip);

    if (infoRec->WriteBitmap && (nglyph > 1) &&
        ((FONTMAXBOUNDS(font, rightSideBearing) -
          FONTMINBOUNDS(font, leftSideBearing)) <= 32)) {
        PolyGlyphBltAsSingleBitmap(pScrn, nglyph, font,
                                   xInit, yInit, nbox, pbox,
                                   fg, GXcopy, planemask);

        return;
    }

    /* compute an approximate but covering bounding box */
    Left = xInit + infoRec->GlyphInfo[0].start;
    Right = xInit + infoRec->GlyphInfo[nglyph - 1].end;
    Top = yInit - FONTMAXBOUNDS(font, ascent);
    Bottom = yInit + FONTMAXBOUNDS(font, descent);

    /* get into the first band that may contain part of our box */
    while (nbox && (Top >= pbox->y2)) {
        pbox++;
        nbox--;
    }

    /* stop when the lower edge of the box is beyond our string */
    while (nbox && (Bottom >= pbox->y1)) {
        LeftEdge = max(Left, pbox->x1);
        RightEdge = min(Right, pbox->x2);

        if (RightEdge > LeftEdge) {     /* we're possibly drawing something */
            ytop = max(Top, pbox->y1);
            ybot = min(Bottom, pbox->y2);
            if (ybot > ytop) {
                skippix = LeftEdge - xInit;
                skipglyph = 0;
                while (skippix >= infoRec->GlyphInfo[skipglyph].end)
                    skipglyph++;

                skippix = RightEdge - xInit;
                n = 0;
                i = skipglyph;
                while ((i < nglyph) && (skippix > infoRec->GlyphInfo[i].start)) {
                    i++;
                    n++;
                }

                if (n)
                    (*infoRec->NonTEGlyphRenderer) (pScrn,
                                                    xInit, yInit, n,
                                                    infoRec->GlyphInfo +
                                                    skipglyph, pbox, fg, GXcopy,
                                                    planemask);
            }
        }

        nbox--;
        pbox++;
    }
}

static int
PolyGlyphBltNonTEColorExpansion(ScrnInfoPtr pScrn,
                                int xInit, int yInit,
                                FontPtr font,
                                int fg, int rop,
                                unsigned planemask,
                                RegionPtr cclip,
                                int nglyph,
                                unsigned char *gBase, CharInfoPtr * ppci)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int skippix, skipglyph, width, n, i;
    int Left, Right, Top, Bottom;
    int LeftEdge, RightEdge;
    int nbox = RegionNumRects(cclip);
    BoxPtr pbox = RegionRects(cclip);

    width = CollectCharacterInfo(infoRec->GlyphInfo, nglyph, ppci, font);

    if (!nbox)
        return width;

    if ((infoRec->WriteBitmap) && (rop == GXcopy) && (nglyph > 1) &&
        ((FONTMAXBOUNDS(font, rightSideBearing) -
          FONTMINBOUNDS(font, leftSideBearing)) <= 32)) {

        PolyGlyphBltAsSingleBitmap(pScrn, nglyph, font,
                                   xInit, yInit, nbox, pbox,
                                   fg, rop, planemask);

        return width;
    }

    /* compute an approximate but covering bounding box */
    Left = xInit + infoRec->GlyphInfo[0].start;
    Right = xInit + infoRec->GlyphInfo[nglyph - 1].end;
    Top = yInit - FONTMAXBOUNDS(font, ascent);
    Bottom = yInit + FONTMAXBOUNDS(font, descent);

    /* get into the first band that may contain part of our string */
    while (nbox && (Top >= pbox->y2)) {
        pbox++;
        nbox--;
    }

    /* stop when the lower edge of the box is beyond our string */
    while (nbox && (Bottom >= pbox->y1)) {
        LeftEdge = max(Left, pbox->x1);
        RightEdge = min(Right, pbox->x2);

        if (RightEdge > LeftEdge) {     /* we're possibly drawing something */

            skippix = LeftEdge - xInit;
            skipglyph = 0;
            while (skippix >= infoRec->GlyphInfo[skipglyph].end)
                skipglyph++;

            skippix = RightEdge - xInit;
            n = 0;
            i = skipglyph;
            while ((i < nglyph) && (skippix > infoRec->GlyphInfo[i].start)) {
                i++;
                n++;
            }

            if (n)
                (*infoRec->NonTEGlyphRenderer) (pScrn,
                                                xInit, yInit, n,
                                                infoRec->GlyphInfo + skipglyph,
                                                pbox, fg, rop, planemask);
        }

        nbox--;
        pbox++;
    }
    return width;
}

/* It is possible that the none of the glyphs passed to the 
   NonTEGlyphRenderer will be drawn.  This function being called
   indicates that part of the text string's bounding box is visible
   but not necessarily that any of the characters are visible */

void
XAANonTEGlyphRenderer(ScrnInfoPtr pScrn,
                      int x, int y, int n,
                      NonTEGlyphPtr glyphs,
                      BoxPtr pbox, int fg, int rop, unsigned int planemask)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x1, x2, y1, y2, i, w, h, skipleft, skiptop;
    unsigned char *src;

    for (i = 0; i < n; i++, glyphs++) {
        x1 = x + glyphs->start;
        x2 = x + glyphs->end;
        y1 = y - glyphs->yoff;
        y2 = y1 + glyphs->height;

        if (y1 < pbox->y1) {
            skiptop = pbox->y1 - y1;
            y1 = pbox->y1;
        }
        else
            skiptop = 0;
        if (y2 > pbox->y2)
            y2 = pbox->y2;
        h = y2 - y1;
        if (h <= 0)
            continue;

        if (x1 < pbox->x1) {
            skipleft = pbox->x1 - x1;
            x1 = pbox->x1;
        }
        else
            skipleft = 0;
        if (x2 > pbox->x2)
            x2 = pbox->x2;

        w = x2 - x1;

        if (w > 0) {
            src = glyphs->bits + (skiptop * glyphs->srcwidth);

            if (skipleft) {
                src += (skipleft >> 5) << 2;
                skipleft &= 31;
            }

            (*infoRec->WriteBitmap) (pScrn, x1, y1, w, h, src,
                                     glyphs->srcwidth, skipleft, fg, -1, rop,
                                     planemask);
        }
    }

}
