
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xaa.h"
#include "xaalocal.h"
#include "xaacexp.h"
#include "xf86.h"

/* Not used anymore because the algorithm isn't correct. It doesn't
   handle overlapping characters properly */

#ifdef TRIPLE_BITS
#define NonTEGlyphFunc EXPNAME(XAANonTEGlyphScanlineFunc3)
#else
#define NonTEGlyphFunc EXPNAME(XAANonTEGlyphScanlineFunc)
#endif

/********************************************************************

   Here we have NonTEGlyphRenders for a bunch of different color
   expansion types.  The driver may provide its own renderer, but
   this is the default one which renders using lower-level primitives
   exported by the chipset driver.

********************************************************************/

/* Since the dimensions of the text string and the backing rectangle
	do not always coincide, it is possible that wBack or wText
	may be 0!  The NonTEGlyphRender must always check for this. */

/* This gets built for MSBFIRST or LSBFIRST with FIXEDBASE or not,
	with TRIPLE_BITS or not. A total of 8 versions */

/* if the backing rectangle and text are of the same dimensions
	then we can draw in one pass */

void
#ifdef TRIPLE_BITS
 EXPNAME(XAANonTEGlyphRenderer3) (
#else
 EXPNAME(XAANonTEGlyphRenderer) (
#endif
                                    ScrnInfoPtr pScrn,
                                    int xText, int wText,
                                    int y, int h, int skipleft, int startline,
                                    NonTEGlyphInfo * glyphp,
                                    int fg, int rop, unsigned int planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base = (CARD32 *) infoRec->ColorExpandBase;

#ifdef TRIPLE_BITS
    int dwords = ((3 * wText + 31) >> 5) * h;
#else
    int dwords = ((wText + 31) >> 5) * h;
#endif

    (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn, fg, -1, rop,
                                                    planemask);
    (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, xText, y, wText, h,
                                                      0);

#ifndef FIXEDBASE
#ifdef TRIPLE_BITS
    if ((((3 * wText + 31) >> 5) * h) <= infoRec->ColorExpandRange)
#else
    if ((((wText + 31) >> 5) * h) <= infoRec->ColorExpandRange)
#endif
        while (h--)
            base = NonTEGlyphFunc(base, glyphp, startline++, wText, skipleft);
    else
#endif
        while (h--)
            NonTEGlyphFunc(base, glyphp, startline++, wText, skipleft);

    if ((infoRec->CPUToScreenColorExpandFillFlags & CPU_TRANSFER_PAD_QWORD) &&
        (dwords & 1)) {
        base = (CARD32 *) infoRec->ColorExpandBase;
        base[0] = 0x00000000;
    }

    if (infoRec->CPUToScreenColorExpandFillFlags & SYNC_AFTER_COLOR_EXPAND)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

#ifndef FIXEDBASE
/*  Scanline version of above gets built for LSBFIRST and MSBFIRST */

void
#ifdef TRIPLE_BITS
 EXPNAME(XAANonTEGlyphRendererScanline3) (
#else
 EXPNAME(XAANonTEGlyphRendererScanline) (
#endif
                                            ScrnInfoPtr pScrn,
                                            int xText, int wText,
                                            int y, int h, int skipleft,
                                            int startline,
                                            NonTEGlyphInfo * glyphp, int fg,
                                            int rop, unsigned int planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int bufferNo = 0;
    CARD32 *base;

    (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn, fg, -1, rop,
                                                            planemask);
    (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn, xText, y,
                                                              wText, h, 0);

    while (h--) {
        base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[bufferNo];
        NonTEGlyphFunc(base, glyphp, startline++, wText, skipleft);
        (*infoRec->SubsequentColorExpandScanline) (pScrn, bufferNo++);
        if (bufferNo >= infoRec->NumScanlineColorExpandBuffers)
            bufferNo = 0;
    }

    SET_SYNC_FLAG(infoRec);
}

#endif

/********************************************************************

   Generic NonTE scanline rendering code.

********************************************************************/

CARD32 *
NonTEGlyphFunc(CARD32 *base,
               NonTEGlyphInfo * glyphp, int line, int TotalWidth, int skipleft)
{
    CARD32 bits = 0;
    int shift = glyphp->width;

    if (skipleft) {
        if ((line >= glyphp->firstline) && (line <= glyphp->lastline))
            bits = SHIFT_R(glyphp->bitsp[line], skipleft);
        shift -= skipleft;
    }
    else if ((line >= glyphp->firstline) && (line <= glyphp->lastline))
        bits = glyphp->bitsp[line];

    while (TotalWidth > 32) {
        while (shift < 32) {
            glyphp++;
            if ((line >= glyphp->firstline) && (line <= glyphp->lastline))
                bits |= SHIFT_L(glyphp->bitsp[line], shift);
            shift += glyphp->width;
        }
#ifdef TRIPLE_BITS
        WRITE_BITS3(bits);
#else
        WRITE_BITS(bits);
#endif
        shift &= 31;
        if (shift && (line >= glyphp->firstline) && (line <= glyphp->lastline))
            bits = SHIFT_R(glyphp->bitsp[line], glyphp->width - shift);
        else
            bits = 0;
        TotalWidth -= 32;
    }

    if (TotalWidth) {
        TotalWidth -= shift;
        while (TotalWidth > 0) {
            glyphp++;
            if ((line >= glyphp->firstline) && (line <= glyphp->lastline))
                bits |= SHIFT_L(glyphp->bitsp[line], shift);
            shift += glyphp->width;
            TotalWidth -= glyphp->width;
        }
#ifdef TRIPLE_BITS
        if (shift >= 22) {
            WRITE_BITS3(bits);
        }
        else if (shift >= 11) {
            WRITE_BITS2(bits);
        }
        else {
            WRITE_BITS1(bits);
        }
#else
        WRITE_BITS(bits);
#endif
    }

    return base;
}
