
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xaa.h"
#include "xaalocal.h"
#include "xaacexp.h"
#include "xf86.h"

/********** byte swapping ***************/

#ifdef FIXEDBASE
#define DEST(i)	*dest
#define RETURN(i)	return(dest)
#else
#define DEST(i)	dest[i]
#define RETURN(i)	return(dest + i)
#endif

#ifdef MSBFIRST
#define SOURCE(i)	SWAP_BITS_IN_BYTES(src[i])
#else
#define SOURCE(i)	src[i]
#endif

typedef CARD32 *(*BitmapScanlineProcPtr) (CARD32 *, CARD32 *, int, int);

#ifdef TRIPLE_BITS
static CARD32 *
BitmapScanline(CARD32 *src, CARD32 *base, int count, int skipleft)
{
    CARD32 bits;

    while (count >= 3) {
        bits = *src;
        WRITE_BITS3(bits);
        src++;
        count -= 3;
    }
    if (count == 2) {
        bits = *src;
        WRITE_BITS2(bits);
    }
    else if (count == 1) {
        bits = *src;
        WRITE_BITS1(bits);
    }

    return base;
}

static CARD32 *
BitmapScanline_Inverted(CARD32 *src, CARD32 *base, int count, int skipleft)
{
    CARD32 bits;

    while (count >= 3) {
        bits = ~(*src);
        WRITE_BITS3(bits);
        src++;
        count -= 3;
    }
    if (count == 2) {
        bits = ~(*src);
        WRITE_BITS2(bits);
    }
    else if (count == 1) {
        bits = ~(*src);
        WRITE_BITS1(bits);
    }

    return base;
}

static CARD32 *
BitmapScanline_Shifted(CARD32 *src, CARD32 *base, int count, int skipleft)
{
    CARD32 bits;

    while (count >= 3) {
        bits = SHIFT_R(*src, skipleft) | SHIFT_L(*(src + 1), (32 - skipleft));
        WRITE_BITS3(bits);
        src++;
        count -= 3;
    }
    if (count == 2) {
        bits = SHIFT_R(*src, skipleft) | SHIFT_L(*(src + 1), (32 - skipleft));
        WRITE_BITS2(bits);
    }
    else if (count == 1) {
        bits = SHIFT_R(*src, skipleft) | SHIFT_L(*(src + 1), (32 - skipleft));
        WRITE_BITS1(bits);
    }

    return base;
}

static CARD32 *
BitmapScanline_Shifted_Inverted(CARD32 *src, CARD32 *base,
                                int count, int skipleft)
{
    CARD32 bits;

    while (count >= 3) {
        bits =
            ~(SHIFT_R(*src, skipleft) | SHIFT_L(*(src + 1), (32 - skipleft)));
        WRITE_BITS3(bits);
        src++;
        count -= 3;
    }
    if (count == 2) {
        bits =
            ~(SHIFT_R(*src, skipleft) | SHIFT_L(*(src + 1), (32 - skipleft)));
        WRITE_BITS2(bits);
    }
    else if (count == 1) {
        bits =
            ~(SHIFT_R(*src, skipleft) | SHIFT_L(*(src + 1), (32 - skipleft)));
        WRITE_BITS1(bits);
    }

    return base;
}

#define BitmapScanline_Shifted_Careful BitmapScanline_Shifted
#define BitmapScanline_Shifted_Inverted_Careful BitmapScanline_Shifted_Inverted

#else
static CARD32 *
BitmapScanline(CARD32 *src, CARD32 *dest, int count, int skipleft)
{
    while (count >= 4) {
        DEST(0) = SOURCE(0);
        DEST(1) = SOURCE(1);
        DEST(2) = SOURCE(2);
        DEST(3) = SOURCE(3);
        count -= 4;
        src += 4;
#ifndef FIXEDBASE
        dest += 4;
#endif
    }

    if (!count)
        return dest;
    DEST(0) = SOURCE(0);
    if (count == 1)
        RETURN(1);
    DEST(1) = SOURCE(1);
    if (count == 2)
        RETURN(2);
    DEST(2) = SOURCE(2);
    RETURN(3);
}

static CARD32 *
BitmapScanline_Inverted(CARD32 *src, CARD32 *dest, int count, int skipleft)
{
    while (count >= 4) {
        DEST(0) = ~SOURCE(0);
        DEST(1) = ~SOURCE(1);
        DEST(2) = ~SOURCE(2);
        DEST(3) = ~SOURCE(3);
        count -= 4;
        src += 4;
#ifndef FIXEDBASE
        dest += 4;
#endif
    }

    if (!count)
        return dest;
    DEST(0) = ~SOURCE(0);
    if (count == 1)
        RETURN(1);
    DEST(1) = ~SOURCE(1);
    if (count == 2)
        RETURN(2);
    DEST(2) = ~SOURCE(2);
    RETURN(3);
}

static CARD32 *
BitmapScanline_Shifted(CARD32 *bits, CARD32 *base, int count, int skipleft)
{
    while (count--) {
        register CARD32 tmp = SHIFT_R(*bits, skipleft) |
            SHIFT_L(*(bits + 1), (32 - skipleft));
        WRITE_BITS(tmp);
        bits++;
    }
    return base;
}

static CARD32 *
BitmapScanline_Shifted_Inverted(CARD32 *bits, CARD32 *base,
                                int count, int skipleft)
{
    while (count--) {
        register CARD32 tmp = ~(SHIFT_R(*bits, skipleft) |
                                SHIFT_L(*(bits + 1), (32 - skipleft)));
        WRITE_BITS(tmp);
        bits++;
    }
    return base;
}

static CARD32 *
BitmapScanline_Shifted_Careful(CARD32 *bits, CARD32 *base,
                               int count, int skipleft)
{
    register CARD32 tmp;

    while (--count) {
        tmp = SHIFT_R(*bits, skipleft) | SHIFT_L(*(bits + 1), (32 - skipleft));
        WRITE_BITS(tmp);
        bits++;
    }
    tmp = SHIFT_R(*bits, skipleft);
    WRITE_BITS(tmp);

    return base;
}

static CARD32 *
BitmapScanline_Shifted_Inverted_Careful(CARD32 *bits, CARD32 *base,
                                        int count, int skipleft)
{
    register CARD32 tmp;

    while (--count) {
        tmp =
            ~(SHIFT_R(*bits, skipleft) | SHIFT_L(*(bits + 1), (32 - skipleft)));
        WRITE_BITS(tmp);
        bits++;
    }
    tmp = ~(SHIFT_R(*bits, skipleft));
    WRITE_BITS(tmp);
    return base;
}

#endif

/*  
    When the accelerator is TRANSPARENCY_ONLY, WriteBitmap can do
    the fill in two passes, inverting the source on the second pass.  
    For GXcopy we can fill the backing rectangle as a solid rect and
    avoid the invert.
*/

void
#ifdef TRIPLE_BITS
 EXPNAME(XAAWriteBitmapColorExpand3) (
#else
 EXPNAME(XAAWriteBitmapColorExpand) (
#endif
                                        ScrnInfoPtr pScrn,
                                        int x, int y, int w, int H,
                                        unsigned char *src,
                                        int srcwidth,
                                        int skipleft,
                                        int fg, int bg,
                                        int rop, unsigned int planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base;
    unsigned char *srcp = src;
    int SecondPassColor = -1;
    int shift = 0, dwords;
    BitmapScanlineProcPtr firstFunc;
    BitmapScanlineProcPtr secondFunc;
    int flag;
    int h = H;

#ifdef TRIPLE_BITS
    if ((bg != -1) &&
        ((infoRec->CPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY) ||
         ((infoRec->CPUToScreenColorExpandFillFlags & RGB_EQUAL) &&
          (!CHECK_RGB_EQUAL(bg))))) {
#else
    if ((bg != -1) &&
        (infoRec->CPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)) {
#endif
        if ((rop == GXcopy) && infoRec->SetupForSolidFill) {
            (*infoRec->SetupForSolidFill) (pScrn, bg, rop, planemask);
            (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, w, h);
        }
        else
            SecondPassColor = bg;
        bg = -1;
    }

#ifdef TRIPLE_BITS
    if (skipleft) {
#else
    if (skipleft &&
        (!(infoRec->CPUToScreenColorExpandFillFlags & LEFT_EDGE_CLIPPING) ||
         (!(infoRec->
            CPUToScreenColorExpandFillFlags & LEFT_EDGE_CLIPPING_NEGATIVE_X) &&
          (skipleft > x)))) {
#endif
        if ((skipleft + ((w + 31) & ~31)) > ((skipleft + w + 31) & ~31)) {
            /* don't read past the end */
            firstFunc = BitmapScanline_Shifted_Careful;
            secondFunc = BitmapScanline_Shifted_Inverted_Careful;
        }
        else {
            firstFunc = BitmapScanline_Shifted;
            secondFunc = BitmapScanline_Shifted_Inverted;
        }
        shift = skipleft;
        skipleft = 0;
    }
    else {
        firstFunc = BitmapScanline;
        secondFunc = BitmapScanline_Inverted;
        w += skipleft;
        x -= skipleft;
    }

#ifdef TRIPLE_BITS
    dwords = (3 * w + 31) >> 5;
#else
    dwords = (w + 31) >> 5;
#endif

 SECOND_PASS:

    flag = (infoRec->CPUToScreenColorExpandFillFlags
            & CPU_TRANSFER_PAD_QWORD) && ((dwords * h) & 0x01);
    (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                    planemask);
    (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, x, y, w, h,
                                                      skipleft);

    base = (CARD32 *) infoRec->ColorExpandBase;

#ifndef FIXEDBASE
    if ((dwords * h) <= infoRec->ColorExpandRange)
        while (h--) {
            base = (*firstFunc) ((CARD32 *) srcp, base, dwords, shift);
            srcp += srcwidth;
        }
    else
#endif
        while (h--) {
            (*firstFunc) ((CARD32 *) srcp, base, dwords, shift);
            srcp += srcwidth;
        }

    if (flag) {
        base = (CARD32 *) infoRec->ColorExpandBase;
        base[0] = 0x00000000;
    }

    if (SecondPassColor != -1) {
        h = H;                  /* Reset height */
        fg = SecondPassColor;
        SecondPassColor = -1;
        firstFunc = secondFunc;
        srcp = src;
        goto SECOND_PASS;
    }

    if (infoRec->CPUToScreenColorExpandFillFlags & SYNC_AFTER_COLOR_EXPAND)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

#ifndef FIXEDBASE

void
#ifdef TRIPLE_BITS
 EXPNAME(XAAWriteBitmapScanlineColorExpand3) (
#else
 EXPNAME(XAAWriteBitmapScanlineColorExpand) (
#endif
                                                ScrnInfoPtr pScrn,
                                                int x, int y, int w, int h,
                                                unsigned char *src,
                                                int srcwidth,
                                                int skipleft,
                                                int fg, int bg,
                                                int rop,
                                                unsigned int planemask) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base;
    unsigned char *srcp = src;
    int SecondPassColor = -1;
    int shift = 0, dwords, bufferNo;
    BitmapScanlineProcPtr firstFunc;
    BitmapScanlineProcPtr secondFunc;

#ifdef TRIPLE_BITS
    if ((bg != -1) &&
        ((infoRec->ScanlineCPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)
         || ((infoRec->ScanlineCPUToScreenColorExpandFillFlags & RGB_EQUAL) &&
             (!CHECK_RGB_EQUAL(bg))))) {
#else
    if ((bg != -1) &&
        (infoRec->
         ScanlineCPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)) {
#endif
        if ((rop == GXcopy) && infoRec->SetupForSolidFill) {
            (*infoRec->SetupForSolidFill) (pScrn, bg, rop, planemask);
            (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, w, h);
        }
        else
            SecondPassColor = bg;
        bg = -1;
    }

#ifdef TRIPLE_BITS
    if (skipleft) {
#else
    if (skipleft &&
        (!(infoRec->ScanlineCPUToScreenColorExpandFillFlags &
           LEFT_EDGE_CLIPPING) ||
         (!(infoRec->ScanlineCPUToScreenColorExpandFillFlags &
            LEFT_EDGE_CLIPPING_NEGATIVE_X) && (skipleft > x)))) {
#endif
        if ((skipleft + ((w + 31) & ~31)) > ((skipleft + w + 31) & ~31)) {
            /* don't read past the end */
            firstFunc = BitmapScanline_Shifted_Careful;
            secondFunc = BitmapScanline_Shifted_Inverted_Careful;
        }
        else {
            firstFunc = BitmapScanline_Shifted;
            secondFunc = BitmapScanline_Shifted_Inverted;
        }
        shift = skipleft;
        skipleft = 0;
    }
    else {
        firstFunc = BitmapScanline;
        secondFunc = BitmapScanline_Inverted;
        w += skipleft;
        x -= skipleft;
    }

#ifdef TRIPLE_BITS
    dwords = (3 * w + 31) >> 5;
#else
    dwords = (w + 31) >> 5;
#endif

 SECOND_PASS:

    (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                            planemask);
    (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn, x, y, w, h,
                                                              skipleft);

    bufferNo = 0;

    while (h--) {
        base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[bufferNo];
        (*firstFunc) ((CARD32 *) srcp, base, dwords, shift);
        (*infoRec->SubsequentColorExpandScanline) (pScrn, bufferNo++);
        srcp += srcwidth;
        if (bufferNo >= infoRec->NumScanlineColorExpandBuffers)
            bufferNo = 0;
    }

    if (SecondPassColor != -1) {
        fg = SecondPassColor;
        SecondPassColor = -1;
        firstFunc = secondFunc;
        srcp = src;
        goto SECOND_PASS;
    }

    SET_SYNC_FLAG(infoRec);
}

#endif
