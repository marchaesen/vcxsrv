
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xaa.h"
#include "xaalocal.h"
#include "xaacexp.h"
#include "xf86.h"

/* scanline function for TRIPLE_BITS_24BPP */
static CARD32 *DrawTextScanline3(CARD32 *base, CARD32 *mem, int width);

/* Loop unrolled functions for common font widths */
static CARD32 *DrawTETextScanlineGeneric(CARD32 *base, unsigned int **glyphp,
                                         int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth7(CARD32 *base, unsigned int **glyphp,
                                        int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth10(CARD32 *base, unsigned int **glyphp,
                                         int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth12(CARD32 *base, unsigned int **glyphp,
                                         int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth14(CARD32 *base, unsigned int **glyphp,
                                         int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth16(CARD32 *base, unsigned int **glyphp,
                                         int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth18(CARD32 *base, unsigned int **glyphp,
                                         int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth24(CARD32 *base, unsigned int **glyphp,
                                         int line, int width, int glyphwidth);

#ifdef USEASSEMBLER
#ifdef FIXEDBASE
#ifdef MSBFIRST
CARD32 *DrawTETextScanlineWidth6PMSBFirstFixedBase(CARD32 *base,
                                                   unsigned int **glyphp,
                                                   int line, int width,
                                                   int glyphwidth);
CARD32 *DrawTETextScanlineWidth8PMSBFirstFixedBase(CARD32 *base,
                                                   unsigned int **glyphp,
                                                   int line, int width,
                                                   int glyphwidth);
CARD32 *DrawTETextScanlineWidth9PMSBFirstFixedBase(CARD32 *base,
                                                   unsigned int **glyphp,
                                                   int line, int width,
                                                   int glyphwidth);
#else
CARD32 *DrawTETextScanlineWidth6PLSBFirstFixedBase(CARD32 *base,
                                                   unsigned int **glyphp,
                                                   int line, int width,
                                                   int glyphwidth);
CARD32 *DrawTETextScanlineWidth8PLSBFirstFixedBase(CARD32 *base,
                                                   unsigned int **glyphp,
                                                   int line, int width,
                                                   int glyphwidth);
CARD32 *DrawTETextScanlineWidth9PLSBFirstFixedBase(CARD32 *base,
                                                   unsigned int **glyphp,
                                                   int line, int width,
                                                   int glyphwidth);
#endif
#else
#ifdef MSBFIRST
CARD32 *DrawTETextScanlineWidth6PMSBFirst(CARD32 *base, unsigned int **glyphp,
                                          int line, int width, int glyphwidth);
CARD32 *DrawTETextScanlineWidth8PMSBFirst(CARD32 *base, unsigned int **glyphp,
                                          int line, int width, int glyphwidth);
CARD32 *DrawTETextScanlineWidth9PMSBFirst(CARD32 *base, unsigned int **glyphp,
                                          int line, int width, int glyphwidth);
#else
CARD32 *DrawTETextScanlineWidth6PLSBFirst(CARD32 *base, unsigned int **glyphp,
                                          int line, int width, int glyphwidth);
CARD32 *DrawTETextScanlineWidth8PLSBFirst(CARD32 *base, unsigned int **glyphp,
                                          int line, int width, int glyphwidth);
CARD32 *DrawTETextScanlineWidth9PLSBFirst(CARD32 *base, unsigned int **glyphp,
                                          int line, int width, int glyphwidth);
#endif
#endif
#else
static CARD32 *DrawTETextScanlineWidth6(CARD32 *base, unsigned int **glyphp,
                                        int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth8(CARD32 *base, unsigned int **glyphp,
                                        int line, int width, int glyphwidth);
static CARD32 *DrawTETextScanlineWidth9(CARD32 *base, unsigned int **glyphp,
                                        int line, int width, int glyphwidth);
#endif

#define glyph_scanline_func EXPNAME(XAAGlyphScanlineFunc)
#define glyph_get_scanline_func EXPNAME(XAAGetGlyphScanlineFunc)

GlyphScanlineFuncPtr glyph_scanline_func[32] = {
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric,
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric,
    DrawTETextScanlineGeneric,
#ifdef USEASSEMBLER
#ifdef FIXEDBASE
#ifdef MSBFIRST
    DrawTETextScanlineWidth6PMSBFirstFixedBase,
    DrawTETextScanlineWidth7,
    DrawTETextScanlineWidth8PMSBFirstFixedBase,
    DrawTETextScanlineWidth9PMSBFirstFixedBase,
#else
    DrawTETextScanlineWidth6PLSBFirstFixedBase,
    DrawTETextScanlineWidth7,
    DrawTETextScanlineWidth8PLSBFirstFixedBase,
    DrawTETextScanlineWidth9PLSBFirstFixedBase,
#endif
#else
#ifdef MSBFIRST
    DrawTETextScanlineWidth6PMSBFirst,
    DrawTETextScanlineWidth7,
    DrawTETextScanlineWidth8PMSBFirst,
    DrawTETextScanlineWidth9PMSBFirst,
#else
    DrawTETextScanlineWidth6PLSBFirst,
    DrawTETextScanlineWidth7,
    DrawTETextScanlineWidth8PLSBFirst,
    DrawTETextScanlineWidth9PLSBFirst,
#endif
#endif
#else
    DrawTETextScanlineWidth6, DrawTETextScanlineWidth7,
    DrawTETextScanlineWidth8, DrawTETextScanlineWidth9,
#endif
    DrawTETextScanlineWidth10,
    DrawTETextScanlineGeneric, DrawTETextScanlineWidth12,
    DrawTETextScanlineGeneric, DrawTETextScanlineWidth14,
    DrawTETextScanlineGeneric, DrawTETextScanlineWidth16,
    DrawTETextScanlineGeneric, DrawTETextScanlineWidth18,
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric,
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric,
    DrawTETextScanlineGeneric, DrawTETextScanlineWidth24,
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric,
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric,
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric,
    DrawTETextScanlineGeneric, DrawTETextScanlineGeneric
};

GlyphScanlineFuncPtr *
glyph_get_scanline_func(void)
{
    return glyph_scanline_func;
}

/********************************************************************

   Here we have TEGlyphRenders for a bunch of different color
   expansion types.  The driver may provide its own renderer, but
   this is the default one which renders using lower-level primitives
   exported by the chipset driver.

********************************************************************/

/* This gets built for MSBFIRST or LSBFIRST with FIXEDBASE or not.
	A total of 4 versions */

void

EXPNAME(XAATEGlyphRenderer) (ScrnInfoPtr pScrn,
                             int x, int y, int w, int h, int skipleft,
                             int startline, unsigned int **glyphs,
                             int glyphWidth, int fg, int bg, int rop,
                             unsigned planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base;
    GlyphScanlineFuncPtr GlyphFunc = glyph_scanline_func[glyphWidth - 1];
    int dwords = 0;

    if ((bg != -1) && (infoRec->TEGlyphRendererFlags & TRANSPARENCY_ONLY)) {
        (*infoRec->SetupForSolidFill) (pScrn, bg, rop, planemask);
        (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, w, h);
        bg = -1;
    }

    (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                    planemask);

    if (skipleft &&
        (!(infoRec->TEGlyphRendererFlags & LEFT_EDGE_CLIPPING) ||
         (!(infoRec->TEGlyphRendererFlags & LEFT_EDGE_CLIPPING_NEGATIVE_X) &&
          (skipleft > x)))) {
        /* draw the first character only */

        int count = h, line = startline;
        int width = glyphWidth - skipleft;

        if (width > w)
            width = w;

        (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, x, y, width, h,
                                                          0);

        base = (CARD32 *) infoRec->ColorExpandBase;

        while (count--) {
            register CARD32 tmp = SHIFT_R(glyphs[0][line++], skipleft);

            WRITE_BITS(tmp);
        }

        w -= width;
        if ((infoRec->TEGlyphRendererFlags & CPU_TRANSFER_PAD_QWORD) &&
            ((((width + 31) >> 5) * h) & 1)) {
            base = (CARD32 *) infoRec->ColorExpandBase;
            base[0] = 0x00000000;
        }
        if (!w)
            goto THE_END;
        glyphs++;
        x += width;
        skipleft = 0;           /* nicely aligned again */
    }

    w += skipleft;
    x -= skipleft;
    dwords = ((w + 31) >> 5) * h;

    (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, x, y, w, h,
                                                      skipleft);

    base = (CARD32 *) infoRec->ColorExpandBase;

#ifndef FIXEDBASE
    if ((((w + 31) >> 5) * h) <= infoRec->ColorExpandRange)
        while (h--) {
            base = (*GlyphFunc) (base, glyphs, startline++, w, glyphWidth);
        }
    else
#endif
        while (h--) {
            (*GlyphFunc) (base, glyphs, startline++, w, glyphWidth);
        }

    if ((infoRec->TEGlyphRendererFlags & CPU_TRANSFER_PAD_QWORD) &&
        (dwords & 1)) {
        base = (CARD32 *) infoRec->ColorExpandBase;
        base[0] = 0x00000000;
    }

 THE_END:

    if (infoRec->TEGlyphRendererFlags & SYNC_AFTER_COLOR_EXPAND)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

/********************************************************************
 
   This is the GlyphRenderer for TRIPLE_BITS_24BPP. It renders to a buffer
   with the non FIXEDBASE LSB_FIRST code before tripling, and possibly
   reversing the bits and sending them to the screen

********************************************************************/

void

EXPNAME(XAATEGlyphRenderer3) (ScrnInfoPtr pScrn,
                              int x, int y, int w, int h, int skipleft,
                              int startline, unsigned int **glyphs,
                              int glyphWidth, int fg, int bg, int rop,
                              unsigned planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base, *mem;
    GlyphScanlineFuncPtr GlyphFunc =
        XAAGlyphScanlineFuncLSBFirst[glyphWidth - 1];
    int dwords = 0;

    if ((bg != -1) &&
        ((infoRec->TEGlyphRendererFlags & TRANSPARENCY_ONLY) ||
         ((infoRec->TEGlyphRendererFlags & RGB_EQUAL) &&
          (!CHECK_RGB_EQUAL(bg))))) {
        (*infoRec->SetupForSolidFill) (pScrn, bg, rop, planemask);
        (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, w, h);
        bg = -1;
    }

    (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                    planemask);

    if (skipleft) {
        /* draw the first character only */

        int count = h, line = startline;
        int width = glyphWidth - skipleft;
        CARD32 bits;

        if (width > w)
            width = w;
        (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, x, y, width, h,
                                                          0);

        base = (CARD32 *) infoRec->ColorExpandBase;

        while (count--) {
            bits = SHIFT_R(glyphs[0][line++], skipleft);
            if (width >= 22) {
                WRITE_BITS3(bits);
            }
            else if (width >= 11) {
                WRITE_BITS2(bits);
            }
            else {
                WRITE_BITS1(bits);
            }
        }

        w -= width;
        if ((infoRec->TEGlyphRendererFlags & CPU_TRANSFER_PAD_QWORD) &&
            ((((3 * width + 31) >> 5) * h) & 1)) {
            base = (CARD32 *) infoRec->ColorExpandBase;
            base[0] = 0x00000000;
        }
        if (!w)
            goto THE_END;
        glyphs++;
        x += width;
        skipleft = 0;           /* nicely aligned again */
    }

    dwords = ((3 * w + 31) >> 5) * h;
    mem = (CARD32 *) malloc(((w + 31) >> 3) * sizeof(char));
    if (!mem)
        return;

    (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, x, y, w, h, 0);

    base = (CARD32 *) infoRec->ColorExpandBase;

#ifndef FIXEDBASE
    if ((((3 * w + 31) >> 5) * h) <= infoRec->ColorExpandRange)
        while (h--) {
            (*GlyphFunc) (mem, glyphs, startline++, w, glyphWidth);
            base = DrawTextScanline3(base, mem, w);
        }
    else
#endif
        while (h--) {
            (*GlyphFunc) (mem, glyphs, startline++, w, glyphWidth);
            DrawTextScanline3(base, mem, w);
        }

    free(mem);

    if ((infoRec->TEGlyphRendererFlags & CPU_TRANSFER_PAD_QWORD) &&
        (dwords & 1)) {
        base = (CARD32 *) infoRec->ColorExpandBase;
        base[0] = 0x00000000;
    }

 THE_END:

    if (infoRec->TEGlyphRendererFlags & SYNC_AFTER_COLOR_EXPAND)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

#ifndef FIXEDBASE
/*  Scanline version of above gets built for LSBFIRST and MSBFIRST */

void

EXPNAME(XAATEGlyphRendererScanline) (ScrnInfoPtr pScrn,
                                     int x, int y, int w, int h, int skipleft,
                                     int startline, unsigned int **glyphs,
                                     int glyphWidth, int fg, int bg, int rop,
                                     unsigned planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int bufferNo;
    CARD32 *base;
    GlyphScanlineFuncPtr GlyphFunc = glyph_scanline_func[glyphWidth - 1];

    if ((bg != -1) && (infoRec->TEGlyphRendererFlags & TRANSPARENCY_ONLY)) {
        (*infoRec->SetupForSolidFill) (pScrn, bg, rop, planemask);
        (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, w, h);
        bg = -1;
    }

    (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                            planemask);

    if (skipleft &&
        (!(infoRec->TEGlyphRendererFlags & LEFT_EDGE_CLIPPING) ||
         (!(infoRec->TEGlyphRendererFlags & LEFT_EDGE_CLIPPING_NEGATIVE_X) &&
          (skipleft > x)))) {
        /* draw the first character only */

        int count = h, line = startline;
        int width = glyphWidth - skipleft;

        if (width > w)
            width = w;

        (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn, x, y,
                                                                  width, h, 0);

        bufferNo = 0;

        while (count--) {
            register CARD32 tmp = SHIFT_R(glyphs[0][line++], skipleft);

            base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[bufferNo];
            WRITE_BITS(tmp);
            (*infoRec->SubsequentColorExpandScanline) (pScrn, bufferNo++);
            if (bufferNo >= infoRec->NumScanlineColorExpandBuffers)
                bufferNo = 0;
        }

        w -= width;
        if (!w)
            goto THE_END;
        glyphs++;
        x += width;
        skipleft = 0;           /* nicely aligned again */
    }

    w += skipleft;
    x -= skipleft;

    (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn, x, y, w, h,
                                                              skipleft);

    bufferNo = 0;

    while (h--) {
        base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[bufferNo];
        (*GlyphFunc) (base, glyphs, startline++, w, glyphWidth);
        (*infoRec->SubsequentColorExpandScanline) (pScrn, bufferNo++);
        if (bufferNo >= infoRec->NumScanlineColorExpandBuffers)
            bufferNo = 0;
    }

 THE_END:

    SET_SYNC_FLAG(infoRec);
}

void

EXPNAME(XAATEGlyphRendererScanline3) (ScrnInfoPtr pScrn,
                                      int x, int y, int w, int h, int skipleft,
                                      int startline, unsigned int **glyphs,
                                      int glyphWidth, int fg, int bg, int rop,
                                      unsigned planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int bufferNo;
    CARD32 *base, *mem;
    GlyphScanlineFuncPtr GlyphFunc =
        XAAGlyphScanlineFuncLSBFirst[glyphWidth - 1];

    if ((bg != -1) &&
        ((infoRec->TEGlyphRendererFlags & TRANSPARENCY_ONLY) ||
         ((infoRec->TEGlyphRendererFlags & RGB_EQUAL) &&
          (!CHECK_RGB_EQUAL(bg))))) {
        (*infoRec->SetupForSolidFill) (pScrn, bg, rop, planemask);
        (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, w, h);
        bg = -1;
    }

    (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                            planemask);

    if (skipleft) {
        /* draw the first character only */

        int count = h, line = startline;
        int width = glyphWidth - skipleft;
        CARD32 bits;

        if (width > w)
            width = w;

        (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn, x, y,
                                                                  width, h, 0);

        bufferNo = 0;

        while (count--) {
            base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[bufferNo];
            bits = SHIFT_R(glyphs[0][line++], skipleft);
            if (width >= 22) {
                WRITE_BITS3(bits);
            }
            else if (width >= 11) {
                WRITE_BITS2(bits);
            }
            else {
                WRITE_BITS1(bits);
            }
            (*infoRec->SubsequentColorExpandScanline) (pScrn, bufferNo++);
            if (bufferNo >= infoRec->NumScanlineColorExpandBuffers)
                bufferNo = 0;
        }

        w -= width;
        if (!w)
            goto THE_END;
        glyphs++;
        x += width;
        skipleft = 0;           /* nicely aligned again */
    }

    w += skipleft;
    x -= skipleft;
    mem = (CARD32 *) malloc(((w + 31) >> 3) * sizeof(char));
    if (!mem)
        return;

    (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn, x, y, w, h,
                                                              skipleft);

    bufferNo = 0;

    while (h--) {
        base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[bufferNo];
        (*GlyphFunc) (mem, glyphs, startline++, w, glyphWidth);
        DrawTextScanline3(base, mem, w);
        (*infoRec->SubsequentColorExpandScanline) (pScrn, bufferNo++);
        if (bufferNo >= infoRec->NumScanlineColorExpandBuffers)
            bufferNo = 0;
    }

    free(mem);

 THE_END:

    SET_SYNC_FLAG(infoRec);
}

#endif

/********************************************************************

   TRIPLE_BITS_24BPP scanline rendering code.

********************************************************************/

static CARD32 *
DrawTextScanline3(CARD32 *base, CARD32 *mem, int width)
{

    while (width > 32) {
        WRITE_BITS3(*mem);
        mem++;
        width -= 32;
    }
    if (width) {
        if (width >= 22) {
            WRITE_BITS3(*mem);
        }
        else if (width >= 11) {
            WRITE_BITS2(*mem);
        }
        else {
            WRITE_BITS1(*mem);
        }
    }

    return base;
}

/********************************************************************

   Generic TE scanline rendering code.

********************************************************************/

static CARD32 *
DrawTETextScanlineGeneric(CARD32 *base,
                          unsigned int **glyphp,
                          int line, int width, int glyphwidth)
{
    CARD32 bits = (*glyphp)[line];
    int shift = glyphwidth;

    while (width > 32) {
        while (shift < 32) {
            glyphp++;
            bits |= SHIFT_L((*glyphp)[line], shift);
            shift += glyphwidth;
        }
        WRITE_BITS(bits);
        shift &= 31;
        if (shift)
            bits = SHIFT_R((*glyphp)[line], (glyphwidth - shift));
        else
            bits = 0;
        width -= 32;
    }

    if (width) {
        width -= shift;
        while (width > 0) {
            glyphp++;
            bits |= SHIFT_L((*glyphp)[line], shift);
            shift += glyphwidth;
            width -= glyphwidth;
        }
        WRITE_BITS(bits);
    }

    return base;
}

/********************************************************************

   Loop unrolled TE font scanline rendering code

********************************************************************/

#ifndef USEASSEMBLER
static CARD32 *
DrawTETextScanlineWidth6(CARD32 *base,
                         unsigned int **glyphp,
                         int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 6);
        bits |= SHIFT_L(glyphp[2][line], 12);
        bits |= SHIFT_L(glyphp[3][line], 18);
        bits |= SHIFT_L(glyphp[4][line], 24);
        bits |= SHIFT_L(glyphp[5][line], 30);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[5][line], 2);
        bits |= SHIFT_L(glyphp[6][line], 4);
        bits |= SHIFT_L(glyphp[7][line], 10);
        bits |= SHIFT_L(glyphp[8][line], 16);
        bits |= SHIFT_L(glyphp[9][line], 22);
        bits |= SHIFT_L(glyphp[10][line], 28);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[10][line], 4);
        bits |= SHIFT_L(glyphp[11][line], 2);
        bits |= SHIFT_L(glyphp[12][line], 8);
        bits |= SHIFT_L(glyphp[13][line], 14);
        bits |= SHIFT_L(glyphp[14][line], 20);
        bits |= SHIFT_L(glyphp[15][line], 26);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
#ifndef FIXEDBASE
        base += 3;
#endif
        width -= 96;
        glyphp += 16;
    }
    return base;
}
#endif

static CARD32 *
DrawTETextScanlineWidth7(CARD32 *base,
                         unsigned int **glyphp,
                         int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 7);
        bits |= SHIFT_L(glyphp[2][line], 14);
        bits |= SHIFT_L(glyphp[3][line], 21);
        bits |= SHIFT_L(glyphp[4][line], 28);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[4][line], 4);
        bits |= SHIFT_L(glyphp[5][line], 3);
        bits |= SHIFT_L(glyphp[6][line], 10);
        bits |= SHIFT_L(glyphp[7][line], 17);
        bits |= SHIFT_L(glyphp[8][line], 24);
        bits |= SHIFT_L(glyphp[9][line], 31);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[9][line], 1);
        bits |= SHIFT_L(glyphp[10][line], 6);
        bits |= SHIFT_L(glyphp[11][line], 13);
        bits |= SHIFT_L(glyphp[12][line], 20);
        bits |= SHIFT_L(glyphp[13][line], 27);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
        bits = SHIFT_R(glyphp[13][line], 5);
        bits |= SHIFT_L(glyphp[14][line], 2);
        bits |= SHIFT_L(glyphp[15][line], 9);
        bits |= SHIFT_L(glyphp[16][line], 16);
        bits |= SHIFT_L(glyphp[17][line], 23);
        bits |= SHIFT_L(glyphp[18][line], 30);
        WRITE_IN_BITORDER(base, 3, bits);
        CHECKRETURN(4);
        bits = SHIFT_R(glyphp[18][line], 2);
        bits |= SHIFT_L(glyphp[19][line], 5);
        bits |= SHIFT_L(glyphp[20][line], 12);
        bits |= SHIFT_L(glyphp[21][line], 19);
        bits |= SHIFT_L(glyphp[22][line], 26);
        WRITE_IN_BITORDER(base, 4, bits);
        CHECKRETURN(5);
        bits = SHIFT_R(glyphp[22][line], 6);
        bits |= SHIFT_L(glyphp[23][line], 1);
        bits |= SHIFT_L(glyphp[24][line], 8);
        bits |= SHIFT_L(glyphp[25][line], 15);
        bits |= SHIFT_L(glyphp[26][line], 22);
        bits |= SHIFT_L(glyphp[27][line], 29);
        WRITE_IN_BITORDER(base, 5, bits);
        CHECKRETURN(6);
        bits = SHIFT_R(glyphp[27][line], 3);
        bits |= SHIFT_L(glyphp[28][line], 4);
        bits |= SHIFT_L(glyphp[29][line], 11);
        bits |= SHIFT_L(glyphp[30][line], 18);
        bits |= SHIFT_L(glyphp[31][line], 25);
        WRITE_IN_BITORDER(base, 6, bits);
        CHECKRETURN(7);
#ifndef FIXEDBASE
        base += 7;
#endif
        width -= 224;
        glyphp += 32;
    }
    return base;
}

#ifndef USEASSEMBLER
static CARD32 *
DrawTETextScanlineWidth8(CARD32 *base,
                         unsigned int **glyphp,
                         int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 8);
        bits |= SHIFT_L(glyphp[2][line], 16);
        bits |= SHIFT_L(glyphp[3][line], 24);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = glyphp[4][line];
        bits |= SHIFT_L(glyphp[5][line], 8);
        bits |= SHIFT_L(glyphp[6][line], 16);
        bits |= SHIFT_L(glyphp[7][line], 24);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
#ifndef FIXEDBASE
        base += 2;
#endif
        width -= 64;
        glyphp += 8;
    }
    return base;
}
#endif

#ifndef USEASSEMBLER
static CARD32 *
DrawTETextScanlineWidth9(CARD32 *base,
                         unsigned int **glyphp,
                         int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 9);
        bits |= SHIFT_L(glyphp[2][line], 18);
        bits |= SHIFT_L(glyphp[3][line], 27);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[3][line], 5);
        bits |= SHIFT_L(glyphp[4][line], 4);
        bits |= SHIFT_L(glyphp[5][line], 13);
        bits |= SHIFT_L(glyphp[6][line], 22);
        bits |= SHIFT_L(glyphp[7][line], 31);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[7][line], 1);
        bits |= SHIFT_L(glyphp[8][line], 8);
        bits |= SHIFT_L(glyphp[9][line], 17);
        bits |= SHIFT_L(glyphp[10][line], 26);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
        bits = SHIFT_R(glyphp[10][line], 6);
        bits |= SHIFT_L(glyphp[11][line], 3);
        bits |= SHIFT_L(glyphp[12][line], 12);
        bits |= SHIFT_L(glyphp[13][line], 21);
        bits |= SHIFT_L(glyphp[14][line], 30);
        WRITE_IN_BITORDER(base, 3, bits);
        CHECKRETURN(4);
        bits = SHIFT_R(glyphp[14][line], 2);
        bits |= SHIFT_L(glyphp[15][line], 7);
        bits |= SHIFT_L(glyphp[16][line], 16);
        bits |= SHIFT_L(glyphp[17][line], 25);
        WRITE_IN_BITORDER(base, 4, bits);
        CHECKRETURN(5);
        bits = SHIFT_R(glyphp[17][line], 7);
        bits |= SHIFT_L(glyphp[18][line], 2);
        bits |= SHIFT_L(glyphp[19][line], 11);
        bits |= SHIFT_L(glyphp[20][line], 20);
        bits |= SHIFT_L(glyphp[21][line], 29);
        WRITE_IN_BITORDER(base, 5, bits);
        CHECKRETURN(6);
        bits = SHIFT_R(glyphp[21][line], 3);
        bits |= SHIFT_L(glyphp[22][line], 6);
        bits |= SHIFT_L(glyphp[23][line], 15);
        bits |= SHIFT_L(glyphp[24][line], 24);
        WRITE_IN_BITORDER(base, 6, bits);
        CHECKRETURN(7);
        bits = SHIFT_R(glyphp[24][line], 8);
        bits |= SHIFT_L(glyphp[25][line], 1);
        bits |= SHIFT_L(glyphp[26][line], 10);
        bits |= SHIFT_L(glyphp[27][line], 19);
        bits |= SHIFT_L(glyphp[28][line], 28);
        WRITE_IN_BITORDER(base, 7, bits);
        CHECKRETURN(8);
        bits = SHIFT_R(glyphp[28][line], 4);
        bits |= SHIFT_L(glyphp[29][line], 5);
        bits |= SHIFT_L(glyphp[30][line], 14);
        bits |= SHIFT_L(glyphp[31][line], 23);
        WRITE_IN_BITORDER(base, 8, bits);
        CHECKRETURN(9);
#ifndef FIXEDBASE
        base += 9;
#endif
        width -= 288;
        glyphp += 32;
    }
    return base;
}
#endif

static CARD32 *
DrawTETextScanlineWidth10(CARD32 *base,
                          unsigned int **glyphp,
                          int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 10);
        bits |= SHIFT_L(glyphp[2][line], 20);
        bits |= SHIFT_L(glyphp[3][line], 30);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[3][line], 2);
        bits |= SHIFT_L(glyphp[4][line], 8);
        bits |= SHIFT_L(glyphp[5][line], 18);
        bits |= SHIFT_L(glyphp[6][line], 28);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[6][line], 4);
        bits |= SHIFT_L(glyphp[7][line], 6);
        bits |= SHIFT_L(glyphp[8][line], 16);
        bits |= SHIFT_L(glyphp[9][line], 26);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
        bits = SHIFT_R(glyphp[9][line], 6);
        bits |= SHIFT_L(glyphp[10][line], 4);
        bits |= SHIFT_L(glyphp[11][line], 14);
        bits |= SHIFT_L(glyphp[12][line], 24);
        WRITE_IN_BITORDER(base, 3, bits);
        CHECKRETURN(4);
        bits = SHIFT_R(glyphp[12][line], 8);
        bits |= SHIFT_L(glyphp[13][line], 2);
        bits |= SHIFT_L(glyphp[14][line], 12);
        bits |= SHIFT_L(glyphp[15][line], 22);
        WRITE_IN_BITORDER(base, 4, bits);
        CHECKRETURN(5);
#ifndef FIXEDBASE
        base += 5;
#endif
        width -= 160;
        glyphp += 16;
    }
    return base;
}

static CARD32 *
DrawTETextScanlineWidth12(CARD32 *base,
                          unsigned int **glyphp,
                          int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 12);
        bits |= SHIFT_L(glyphp[2][line], 24);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[2][line], 8);
        bits |= SHIFT_L(glyphp[3][line], 4);
        bits |= SHIFT_L(glyphp[4][line], 16);
        bits |= SHIFT_L(glyphp[5][line], 28);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[5][line], 4);
        bits |= SHIFT_L(glyphp[6][line], 8);
        bits |= SHIFT_L(glyphp[7][line], 20);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
#ifndef FIXEDBASE
        base += 3;
#endif
        width -= 96;
        glyphp += 8;
    }
    return base;
}

static CARD32 *
DrawTETextScanlineWidth14(CARD32 *base,
                          unsigned int **glyphp,
                          int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 14);
        bits |= SHIFT_L(glyphp[2][line], 28);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[2][line], 4);
        bits |= SHIFT_L(glyphp[3][line], 10);
        bits |= SHIFT_L(glyphp[4][line], 24);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[4][line], 8);
        bits |= SHIFT_L(glyphp[5][line], 6);
        bits |= SHIFT_L(glyphp[6][line], 20);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
        bits = SHIFT_R(glyphp[6][line], 12);
        bits |= SHIFT_L(glyphp[7][line], 2);
        bits |= SHIFT_L(glyphp[8][line], 16);
        bits |= SHIFT_L(glyphp[9][line], 30);
        WRITE_IN_BITORDER(base, 3, bits);
        CHECKRETURN(4);
        bits = SHIFT_R(glyphp[9][line], 2);
        bits |= SHIFT_L(glyphp[10][line], 12);
        bits |= SHIFT_L(glyphp[11][line], 26);
        WRITE_IN_BITORDER(base, 4, bits);
        CHECKRETURN(5);
        bits = SHIFT_R(glyphp[11][line], 6);
        bits |= SHIFT_L(glyphp[12][line], 8);
        bits |= SHIFT_L(glyphp[13][line], 22);
        WRITE_IN_BITORDER(base, 5, bits);
        CHECKRETURN(6);
        bits = SHIFT_R(glyphp[13][line], 10);
        bits |= SHIFT_L(glyphp[14][line], 4);
        bits |= SHIFT_L(glyphp[15][line], 18);
        WRITE_IN_BITORDER(base, 6, bits);
        CHECKRETURN(7);
#ifndef FIXEDBASE
        base += 7;
#endif
        width -= 224;
        glyphp += 16;
    }
    return base;
}

static CARD32 *
DrawTETextScanlineWidth16(CARD32 *base,
                          unsigned int **glyphp,
                          int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 16);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = glyphp[2][line];
        bits |= SHIFT_L(glyphp[3][line], 16);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = glyphp[4][line];
        bits |= SHIFT_L(glyphp[5][line], 16);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
        bits = glyphp[6][line];
        bits |= SHIFT_L(glyphp[7][line], 16);
        WRITE_IN_BITORDER(base, 3, bits);
        CHECKRETURN(4);
#ifndef FIXEDBASE
        base += 4;
#endif
        width -= 128;
        glyphp += 8;
    }
    return base;
}

static CARD32 *
DrawTETextScanlineWidth18(CARD32 *base,
                          unsigned int **glyphp,
                          int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 18);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[1][line], 14);
        bits |= SHIFT_L(glyphp[2][line], 4);
        bits |= SHIFT_L(glyphp[3][line], 22);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[3][line], 10);
        bits |= SHIFT_L(glyphp[4][line], 8);
        bits |= SHIFT_L(glyphp[5][line], 26);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
        bits = SHIFT_R(glyphp[5][line], 6);
        bits |= SHIFT_L(glyphp[6][line], 12);
        bits |= SHIFT_L(glyphp[7][line], 30);
        WRITE_IN_BITORDER(base, 3, bits);
        CHECKRETURN(4);
        bits = SHIFT_R(glyphp[7][line], 2);
        bits |= SHIFT_L(glyphp[8][line], 16);
        WRITE_IN_BITORDER(base, 4, bits);
        CHECKRETURN(5);
        bits = SHIFT_R(glyphp[8][line], 16);
        bits |= SHIFT_L(glyphp[9][line], 2);
        bits |= SHIFT_L(glyphp[10][line], 20);
        WRITE_IN_BITORDER(base, 5, bits);
        CHECKRETURN(6);
        bits = SHIFT_R(glyphp[10][line], 12);
        bits |= SHIFT_L(glyphp[11][line], 6);
        bits |= SHIFT_L(glyphp[12][line], 24);
        WRITE_IN_BITORDER(base, 6, bits);
        CHECKRETURN(7);
        bits = SHIFT_R(glyphp[12][line], 8);
        bits |= SHIFT_L(glyphp[13][line], 10);
        bits |= SHIFT_L(glyphp[14][line], 28);
        WRITE_IN_BITORDER(base, 7, bits);
        CHECKRETURN(8);
        bits = SHIFT_R(glyphp[14][line], 4);
        bits |= SHIFT_L(glyphp[15][line], 14);
        WRITE_IN_BITORDER(base, 8, bits);
        CHECKRETURN(9);
#ifndef FIXEDBASE
        base += 9;
#endif
        width -= 288;
        glyphp += 16;
    }
    return base;
}

static CARD32 *
DrawTETextScanlineWidth24(CARD32 *base,
                          unsigned int **glyphp,
                          int line, int width, int glyphwidth)
{
    while (1) {
        unsigned int bits;

        bits = glyphp[0][line];
        bits |= SHIFT_L(glyphp[1][line], 24);
        WRITE_IN_BITORDER(base, 0, bits);
        CHECKRETURN(1);
        bits = SHIFT_R(glyphp[1][line], 8);
        bits |= SHIFT_L(glyphp[2][line], 16);
        WRITE_IN_BITORDER(base, 1, bits);
        CHECKRETURN(2);
        bits = SHIFT_R(glyphp[2][line], 16);
        bits |= SHIFT_L(glyphp[3][line], 8);
        WRITE_IN_BITORDER(base, 2, bits);
        CHECKRETURN(3);
#ifndef FIXEDBASE
        base += 3;
#endif
        width -= 96;
        glyphp += 4;
    }
    return base;
}
