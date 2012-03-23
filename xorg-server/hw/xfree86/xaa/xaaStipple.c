
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xaa.h"
#include "xaalocal.h"
#include "xaacexp.h"
#include "xf86.h"

static CARD32 *StipplePowerOfTwo(CARD32 *, CARD32 *, int, int, int);
static CARD32 *StipplePowerOfTwo_Inverted(CARD32 *, CARD32 *, int, int, int);
static CARD32 *StippleUpTo32(CARD32 *, CARD32 *, int, int, int);
static CARD32 *StippleUpTo32_Inverted(CARD32 *, CARD32 *, int, int, int);
static CARD32 *StippleOver32(CARD32 *, CARD32 *, int, int, int);
static CARD32 *StippleOver32_Inverted(CARD32 *, CARD32 *, int, int, int);

#ifdef TRIPLE_BITS
#define stipple_scanline_func EXPNAME(XAAStippleScanlineFunc3)
#define stipple_get_scanline_func EXPNAME(XAAGetStippleScanlineFunc3)
#else
#define stipple_scanline_func EXPNAME(XAAStippleScanlineFunc)
#define stipple_get_scanline_func EXPNAME(XAAGetStippleScanlineFunc)
#endif

StippleScanlineProcPtr stipple_scanline_func[6] = {
    StipplePowerOfTwo,
    StippleUpTo32,
    StippleOver32,
    StipplePowerOfTwo_Inverted,
    StippleUpTo32_Inverted,
    StippleOver32_Inverted
};

StippleScanlineProcPtr *
stipple_get_scanline_func(void)
{
    return stipple_scanline_func;
}

#ifdef FIXEDBASE
#define DEST(i)	*dest
#define RETURN(i)	return(dest)
#else
#define DEST(i)	dest[i]
#define RETURN(i)	return(dest + i)
#endif

/* TRIPLE_BITS pattern expansion */
#ifdef TRIPLE_BITS
#define EXPAND_PAT \
	CARD32 pat1 = byte_expand3[pat & 0xFF], \
	       pat2 = byte_expand3[(pat & 0xFF00) >> 8], \
	       pat3 = byte_expand3[(pat & 0xFF0000) >> 16], \
	       pat4 = byte_expand3[(pat & 0xFF000000) >> 24], \
	       patA = pat1 | (pat2 << 24), \
	       patB = (pat2 >> 8) | (pat3 << 16), \
	       patC = (pat3 >> 16) | (pat4 << 8)
#ifdef FIXED_BASE
#define WRITE_PAT1 { \
	*dest = patA; }
#define WRITE_PAT2 { \
	*dest = patA; \
	*dest = patB; }
#define WRITE_PAT3 { \
	*dest = patA; \
	*dest = patB; \
	*dest = patC; }
#else
#define WRITE_PAT1 { \
	*(dest++) = patA; }
#define WRITE_PAT2 { \
	*(dest) = patA; \
	*(dest + 1) = patB; \
	dest += 2; }
#define WRITE_PAT3 { \
	*(dest) = patA; \
	*(dest + 1) = patB; \
	*(dest + 2) = patC; \
	dest += 3; }
#endif
#endif

#if !defined(FIXEDBASE) && !defined(MSBFIRST) && !defined(TRIPLE_BITS)

unsigned int XAAShiftMasks[32] = {
    /* gcc is rather pedantic about SHIFT_R(0xFFFFFFFF,32) */
    0x00000000, SHIFT_R(0xFFFFFFFF, 31),
    SHIFT_R(0xFFFFFFFF, 30), SHIFT_R(0xFFFFFFFF, 29),
    SHIFT_R(0xFFFFFFFF, 28), SHIFT_R(0xFFFFFFFF, 27),
    SHIFT_R(0xFFFFFFFF, 26), SHIFT_R(0xFFFFFFFF, 25),
    SHIFT_R(0xFFFFFFFF, 24), SHIFT_R(0xFFFFFFFF, 23),
    SHIFT_R(0xFFFFFFFF, 22), SHIFT_R(0xFFFFFFFF, 21),
    SHIFT_R(0xFFFFFFFF, 20), SHIFT_R(0xFFFFFFFF, 19),
    SHIFT_R(0xFFFFFFFF, 18), SHIFT_R(0xFFFFFFFF, 17),
    SHIFT_R(0xFFFFFFFF, 16), SHIFT_R(0xFFFFFFFF, 15),
    SHIFT_R(0xFFFFFFFF, 14), SHIFT_R(0xFFFFFFFF, 13),
    SHIFT_R(0xFFFFFFFF, 12), SHIFT_R(0xFFFFFFFF, 11),
    SHIFT_R(0xFFFFFFFF, 10), SHIFT_R(0xFFFFFFFF, 9),
    SHIFT_R(0xFFFFFFFF, 8), SHIFT_R(0xFFFFFFFF, 7),
    SHIFT_R(0xFFFFFFFF, 6), SHIFT_R(0xFFFFFFFF, 5),
    SHIFT_R(0xFFFFFFFF, 4), SHIFT_R(0xFFFFFFFF, 3),
    SHIFT_R(0xFFFFFFFF, 2), SHIFT_R(0xFFFFFFFF, 1)
};

#endif

void
#ifdef TRIPLE_BITS
 EXPNAME(XAAFillColorExpandRects3) (
#else
 EXPNAME(XAAFillColorExpandRects) (
#endif
                                      ScrnInfoPtr pScrn,
                                      int fg, int bg, int rop,
                                      unsigned int planemask,
                                      int nBox,
                                      BoxPtr pBox,
                                      int xorg, int yorg, PixmapPtr pPix) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base;
    Bool TwoPass = FALSE, FirstPass = TRUE;
    StippleScanlineProcPtr StippleFunc, FirstFunc, SecondFunc;
    int stipplewidth = pPix->drawable.width;
    int stippleheight = pPix->drawable.height;
    int srcwidth = pPix->devKind;
    int dwords, srcy, srcx, funcNo = 2, h;
    unsigned char *src = (unsigned char *) pPix->devPrivate.ptr;
    unsigned char *srcp;
    int flag;

    if (stipplewidth <= 32) {
        if (stipplewidth & (stipplewidth - 1))
            funcNo = 1;
        else
            funcNo = 0;
    }
    StippleFunc = stipple_scanline_func[funcNo];
    SecondFunc = stipple_scanline_func[funcNo];
    FirstFunc = stipple_scanline_func[funcNo + 3];

#ifdef TRIPLE_BITS
    if ((bg == -1) ||
        (!(infoRec->CPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY) &&
         (!(infoRec->CPUToScreenColorExpandFillFlags & RGB_EQUAL) ||
          (CHECK_RGB_EQUAL(bg))))) {
#else
    if ((bg == -1) ||
        !(infoRec->CPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)) {
#endif
        /* one pass */
    }
    else if ((rop == GXcopy) && infoRec->FillSolidRects) {
        /* one pass but we fill background rects first */
        (*infoRec->FillSolidRects) (pScrn, bg, rop, planemask, nBox, pBox);
        bg = -1;
    }
    else {
        /* gotta do two passes */
        TwoPass = TRUE;
    }

    if (!TwoPass)
        (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                        planemask);

    while (nBox--) {
#ifdef TRIPLE_BITS
        dwords = (3 * (pBox->x2 - pBox->x1) + 31) >> 5;
#else
        dwords = (pBox->x2 - pBox->x1 + 31) >> 5;
#endif

 SECOND_PASS:
        if (TwoPass) {
            (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn,
                                                            (FirstPass) ? bg :
                                                            fg, -1, rop,
                                                            planemask);
            StippleFunc = (FirstPass) ? FirstFunc : SecondFunc;
        }

        h = pBox->y2 - pBox->y1;
        flag = (infoRec->CPUToScreenColorExpandFillFlags
                & CPU_TRANSFER_PAD_QWORD) && ((dwords * h) & 0x01);

        (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, pBox->x1,
                                                          pBox->y1,
                                                          pBox->x2 - pBox->x1,
                                                          h, 0);

        base = (CARD32 *) infoRec->ColorExpandBase;

        srcy = (pBox->y1 - yorg) % stippleheight;
        if (srcy < 0)
            srcy += stippleheight;
        srcx = (pBox->x1 - xorg) % stipplewidth;
        if (srcx < 0)
            srcx += stipplewidth;

        srcp = (srcwidth * srcy) + src;

#ifndef FIXEDBASE
        if ((dwords * h) <= infoRec->ColorExpandRange) {
            while (h--) {
                base =
                    (*StippleFunc) (base, (CARD32 *) srcp, srcx, stipplewidth,
                                    dwords);
                srcy++;
                srcp += srcwidth;
                if (srcy >= stippleheight) {
                    srcy = 0;
                    srcp = src;
                }
            }
        }
        else
#endif
            while (h--) {
                (*StippleFunc) (base, (CARD32 *) srcp, srcx, stipplewidth,
                                dwords);
                srcy++;
                srcp += srcwidth;
                if (srcy >= stippleheight) {
                    srcy = 0;
                    srcp = src;
                }
            }

        if (flag) {
            base = (CARD32 *) infoRec->ColorExpandBase;
            base[0] = 0x00000000;
        }

        if (TwoPass) {
            if (FirstPass) {
                FirstPass = FALSE;
                goto SECOND_PASS;
            }
            else
                FirstPass = TRUE;
        }

        pBox++;
    }

    if (infoRec->CPUToScreenColorExpandFillFlags & SYNC_AFTER_COLOR_EXPAND)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

void
#ifdef TRIPLE_BITS
 EXPNAME(XAAFillColorExpandSpans3) (
#else
 EXPNAME(XAAFillColorExpandSpans) (
#endif
                                      ScrnInfoPtr pScrn,
                                      int fg, int bg, int rop,
                                      unsigned int planemask,
                                      int n,
                                      DDXPointPtr ppt,
                                      int *pwidth,
                                      int fSorted,
                                      int xorg, int yorg, PixmapPtr pPix) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base;
    Bool TwoPass = FALSE, FirstPass = TRUE;
    StippleScanlineProcPtr StippleFunc, FirstFunc, SecondFunc;
    int stipplewidth = pPix->drawable.width;
    int stippleheight = pPix->drawable.height;
    int dwords, srcy, srcx, funcNo = 2;
    unsigned char *srcp;

    if (stipplewidth <= 32) {
        if (stipplewidth & (stipplewidth - 1))
            funcNo = 1;
        else
            funcNo = 0;
    }
    StippleFunc = stipple_scanline_func[funcNo];
    SecondFunc = stipple_scanline_func[funcNo];
    FirstFunc = stipple_scanline_func[funcNo + 3];

#ifdef TRIPLE_BITS
    if ((bg == -1) ||
        (!(infoRec->CPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY) &&
         (!(infoRec->CPUToScreenColorExpandFillFlags & RGB_EQUAL) ||
          (CHECK_RGB_EQUAL(bg))))) {
#else
    if ((bg == -1) ||
        !(infoRec->CPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)) {
#endif
        /* one pass */
    }
    else if ((rop == GXcopy) && infoRec->FillSolidSpans) {
        /* one pass but we fill background rects first */
        (*infoRec->FillSolidSpans) (pScrn, bg, rop, planemask, n, ppt, pwidth,
                                    fSorted);
        bg = -1;
    }
    else {
        /* gotta do two passes */
        TwoPass = TRUE;
    }

    if (!TwoPass)
        (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                        planemask);

    while (n--) {
#ifdef TRIPLE_BITS
        dwords = (3 * *pwidth + 31) >> 5;
#else
        dwords = (*pwidth + 31) >> 5;
#endif

        srcy = (ppt->y - yorg) % stippleheight;
        if (srcy < 0)
            srcy += stippleheight;
        srcx = (ppt->x - xorg) % stipplewidth;
        if (srcx < 0)
            srcx += stipplewidth;

        srcp = (pPix->devKind * srcy) + (unsigned char *) pPix->devPrivate.ptr;

 SECOND_PASS:
        if (TwoPass) {
            (*infoRec->SetupForCPUToScreenColorExpandFill) (pScrn,
                                                            (FirstPass) ? bg :
                                                            fg, -1, rop,
                                                            planemask);
            StippleFunc = (FirstPass) ? FirstFunc : SecondFunc;
        }

        (*infoRec->SubsequentCPUToScreenColorExpandFill) (pScrn, ppt->x, ppt->y,
                                                          *pwidth, 1, 0);

        base = (CARD32 *) infoRec->ColorExpandBase;

        (*StippleFunc) (base, (CARD32 *) srcp, srcx, stipplewidth, dwords);

        if ((infoRec->CPUToScreenColorExpandFillFlags & CPU_TRANSFER_PAD_QWORD)
            && (dwords & 0x01)) {
            base = (CARD32 *) infoRec->ColorExpandBase;
            base[0] = 0x00000000;
        }

        if (TwoPass) {
            if (FirstPass) {
                FirstPass = FALSE;
                goto SECOND_PASS;
            }
            else
                FirstPass = TRUE;
        }

        ppt++;
        pwidth++;
    }

    if (infoRec->CPUToScreenColorExpandFillFlags & SYNC_AFTER_COLOR_EXPAND)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

#ifndef FIXEDBASE

void
#ifdef TRIPLE_BITS
 EXPNAME(XAAFillScanlineColorExpandRects3) (
#else
 EXPNAME(XAAFillScanlineColorExpandRects) (
#endif
                                              ScrnInfoPtr pScrn,
                                              int fg, int bg, int rop,
                                              unsigned int planemask,
                                              int nBox,
                                              BoxPtr pBox,
                                              int xorg, int yorg,
                                              PixmapPtr pPix) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base;
    Bool TwoPass = FALSE, FirstPass = TRUE;
    StippleScanlineProcPtr StippleFunc, FirstFunc, SecondFunc;
    int stipplewidth = pPix->drawable.width;
    int stippleheight = pPix->drawable.height;
    int srcwidth = pPix->devKind;
    int dwords, srcy, srcx, funcNo = 2, bufferNo, h;
    unsigned char *src = pPix->devPrivate.ptr;
    unsigned char *srcp;

    if (stipplewidth <= 32) {
        if (stipplewidth & (stipplewidth - 1))
            funcNo = 1;
        else
            funcNo = 0;
    }
    StippleFunc = stipple_scanline_func[funcNo];
    SecondFunc = stipple_scanline_func[funcNo];
    FirstFunc = stipple_scanline_func[funcNo + 3];

#ifdef TRIPLE_BITS
    if ((bg == -1) ||
        (!(infoRec->ScanlineCPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)
         && (!(infoRec->ScanlineCPUToScreenColorExpandFillFlags & RGB_EQUAL) ||
             (CHECK_RGB_EQUAL(bg))))) {
#else
    if ((bg == -1) ||
        !(infoRec->
          ScanlineCPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)) {
#endif
        /* one pass */
    }
    else if ((rop == GXcopy) && infoRec->FillSolidRects) {
        /* one pass but we fill background rects first */
        (*infoRec->FillSolidRects) (pScrn, bg, rop, planemask, nBox, pBox);
        bg = -1;
    }
    else {
        /* gotta do two passes */
        TwoPass = TRUE;
    }

    if (!TwoPass)
        (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn, fg, bg,
                                                                rop, planemask);

    while (nBox--) {
#ifdef TRIPLE_BITS
        dwords = (3 * (pBox->x2 - pBox->x1) + 31) >> 5;
#else
        dwords = (pBox->x2 - pBox->x1 + 31) >> 5;
#endif

 SECOND_PASS:
        if (TwoPass) {
            (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn,
                                                                    (FirstPass)
                                                                    ? bg : fg,
                                                                    -1, rop,
                                                                    planemask);
            StippleFunc = (FirstPass) ? FirstFunc : SecondFunc;
        }

        h = pBox->y2 - pBox->y1;

        (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn,
                                                                  pBox->x1,
                                                                  pBox->y1,
                                                                  pBox->x2 -
                                                                  pBox->x1, h,
                                                                  0);

        bufferNo = 0;

        srcy = (pBox->y1 - yorg) % stippleheight;
        if (srcy < 0)
            srcy += stippleheight;
        srcx = (pBox->x1 - xorg) % stipplewidth;
        if (srcx < 0)
            srcx += stipplewidth;

        srcp = (srcwidth * srcy) + src;

        while (h--) {
            base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[bufferNo];
            (*StippleFunc) (base, (CARD32 *) srcp, srcx, stipplewidth, dwords);
            (*infoRec->SubsequentColorExpandScanline) (pScrn, bufferNo++);
            if (bufferNo >= infoRec->NumScanlineColorExpandBuffers)
                bufferNo = 0;
            srcy++;
            srcp += srcwidth;
            if (srcy >= stippleheight) {
                srcy = 0;
                srcp = src;
            }
        }

        if (TwoPass) {
            if (FirstPass) {
                FirstPass = FALSE;
                goto SECOND_PASS;
            }
            else
                FirstPass = TRUE;
        }

        pBox++;
    }

    SET_SYNC_FLAG(infoRec);
}

void
#ifdef TRIPLE_BITS
 EXPNAME(XAAFillScanlineColorExpandSpans3) (
#else
 EXPNAME(XAAFillScanlineColorExpandSpans) (
#endif
                                              ScrnInfoPtr pScrn,
                                              int fg, int bg, int rop,
                                              unsigned int planemask,
                                              int n,
                                              DDXPointPtr ppt,
                                              int *pwidth,
                                              int fSorted,
                                              int xorg, int yorg,
                                              PixmapPtr pPix) {
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    CARD32 *base;
    Bool TwoPass = FALSE, FirstPass = TRUE;
    StippleScanlineProcPtr StippleFunc, FirstFunc, SecondFunc;
    int stipplewidth = pPix->drawable.width;
    int stippleheight = pPix->drawable.height;
    int dwords, srcy, srcx, funcNo = 2;
    unsigned char *srcp;

    if (stipplewidth <= 32) {
        if (stipplewidth & (stipplewidth - 1))
            funcNo = 1;
        else
            funcNo = 0;
    }
    StippleFunc = stipple_scanline_func[funcNo];
    SecondFunc = stipple_scanline_func[funcNo];
    FirstFunc = stipple_scanline_func[funcNo + 3];

#ifdef TRIPLE_BITS
    if ((bg == -1) ||
        (!(infoRec->ScanlineCPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)
         && (!(infoRec->ScanlineCPUToScreenColorExpandFillFlags & RGB_EQUAL) ||
             (CHECK_RGB_EQUAL(bg))))) {
#else
    if ((bg == -1) ||
        !(infoRec->
          ScanlineCPUToScreenColorExpandFillFlags & TRANSPARENCY_ONLY)) {
#endif
        /* one pass */
    }
    else if ((rop == GXcopy) && infoRec->FillSolidSpans) {
        /* one pass but we fill background rects first */
        (*infoRec->FillSolidSpans) (pScrn, bg, rop, planemask, n, ppt, pwidth,
                                    fSorted);
        bg = -1;
    }
    else {
        /* gotta do two passes */
        TwoPass = TRUE;
    }

    if (!TwoPass)
        (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn, fg, bg,
                                                                rop, planemask);

    while (n--) {
#ifdef TRIPLE_BITS
        dwords = (3 * *pwidth + 31) >> 5;
#else
        dwords = (*pwidth + 31) >> 5;
#endif

        srcy = (ppt->y - yorg) % stippleheight;
        if (srcy < 0)
            srcy += stippleheight;
        srcx = (ppt->x - xorg) % stipplewidth;
        if (srcx < 0)
            srcx += stipplewidth;

        srcp = (pPix->devKind * srcy) + (unsigned char *) pPix->devPrivate.ptr;

 SECOND_PASS:
        if (TwoPass) {
            (*infoRec->SetupForScanlineCPUToScreenColorExpandFill) (pScrn,
                                                                    (FirstPass)
                                                                    ? bg : fg,
                                                                    -1, rop,
                                                                    planemask);
            StippleFunc = (FirstPass) ? FirstFunc : SecondFunc;
        }

        (*infoRec->SubsequentScanlineCPUToScreenColorExpandFill) (pScrn, ppt->x,
                                                                  ppt->y,
                                                                  *pwidth, 1,
                                                                  0);

        base = (CARD32 *) infoRec->ScanlineColorExpandBuffers[0];

        (*StippleFunc) (base, (CARD32 *) srcp, srcx, stipplewidth, dwords);
        (*infoRec->SubsequentColorExpandScanline) (pScrn, 0);

        if (TwoPass) {
            if (FirstPass) {
                FirstPass = FALSE;
                goto SECOND_PASS;
            }
            else
                FirstPass = TRUE;
        }

        ppt++;
        pwidth++;
    }

    SET_SYNC_FLAG(infoRec);
}

#endif

static CARD32 *
StipplePowerOfTwo(CARD32 *dest, CARD32 *src, int shift, int width, int dwords)
{
    CARD32 pat = *src;

    if (width < 32) {
        pat &= XAAShiftMasks[width];
        while (width < 32) {
            pat |= SHIFT_L(pat, width);
            width <<= 1;
        }
    }

    if (shift)
        pat = SHIFT_R(pat, shift) | SHIFT_L(pat, 32 - shift);

#ifdef MSBFIRST
    pat = SWAP_BITS_IN_BYTES(pat);
#endif

#ifdef TRIPLE_BITS
    {
        EXPAND_PAT;

        while (dwords >= 3) {
            WRITE_PAT3;
            dwords -= 3;
        }
        if (dwords == 2) {
            WRITE_PAT2;
        }
        else if (dwords == 1) {
            WRITE_PAT1;
        }

        return dest;
    }
#else                           /* TRIPLE_BITS */
    while (dwords >= 4) {
        DEST(0) = pat;
        DEST(1) = pat;
        DEST(2) = pat;
        DEST(3) = pat;
        dwords -= 4;
#ifndef FIXEDBASE
        dest += 4;
#endif
    }

    if (!dwords)
        return dest;
    DEST(0) = pat;
    if (dwords == 1)
        RETURN(1);
    DEST(1) = pat;
    if (dwords == 2)
        RETURN(2);
    DEST(2) = pat;
    RETURN(3);
#endif                          /* TRIPLE_BITS */
}

static CARD32 *
StipplePowerOfTwo_Inverted(CARD32 *dest, CARD32 *src,
                           int shift, int width, int dwords)
{
    CARD32 pat = *src;

    if (width < 32) {
        pat &= XAAShiftMasks[width];
        while (width < 32) {
            pat |= SHIFT_L(pat, width);
            width <<= 1;
        }
    }

    if (shift)
        pat = SHIFT_R(pat, shift) | SHIFT_L(pat, 32 - shift);

#ifdef MSBFIRST
    pat = SWAP_BITS_IN_BYTES(pat);
#endif

    pat = ~pat;

#ifdef TRIPLE_BITS
    {
        EXPAND_PAT;

        while (dwords >= 3) {
            WRITE_PAT3;
            dwords -= 3;
        }
        if (dwords == 2) {
            WRITE_PAT2;
        }
        else if (dwords == 1) {
            WRITE_PAT1;
        }

        return dest;
    }
#else                           /* TRIPLE_BITS */
    while (dwords >= 4) {
        DEST(0) = pat;
        DEST(1) = pat;
        DEST(2) = pat;
        DEST(3) = pat;
        dwords -= 4;
#ifndef FIXEDBASE
        dest += 4;
#endif
    }

    if (!dwords)
        return dest;
    DEST(0) = pat;
    if (dwords == 1)
        RETURN(1);
    DEST(1) = pat;
    if (dwords == 2)
        RETURN(2);
    DEST(2) = pat;
    RETURN(3);
#endif                          /* TRIPLE_BITS */
}

static CARD32 *
StippleUpTo32(CARD32 *base, CARD32 *src, int shift, int width, int dwords)
{
    CARD32 pat = *src & XAAShiftMasks[width];

    while (width <= 15) {
        pat |= SHIFT_L(pat, width);
        width <<= 1;
    }
    pat |= SHIFT_L(pat, width);

    while (dwords--) {
        CARD32 bits = SHIFT_R(pat, shift) | SHIFT_L(pat, width - shift);

#ifdef TRIPLE_BITS
        if (dwords >= 2) {
            WRITE_BITS3(bits);
            dwords -= 2;
        }
        else if (dwords > 0) {
            WRITE_BITS2(bits);
            dwords--;
        }
        else {
            WRITE_BITS1(bits);
        }
#else
        WRITE_BITS(bits);
#endif

        shift += 32;
        shift %= width;
    }
    return base;
}

static CARD32 *
StippleUpTo32_Inverted(CARD32 *base, CARD32 *src,
                       int shift, int width, int dwords)
{
    CARD32 pat = *src & XAAShiftMasks[width];

    while (width <= 15) {
        pat |= SHIFT_L(pat, width);
        width <<= 1;
    }
    pat |= SHIFT_L(pat, width);

    while (dwords--) {
        CARD32 bits = ~(SHIFT_R(pat, shift) | SHIFT_L(pat, width - shift));

#ifdef TRIPLE_BITS
        if (dwords >= 2) {
            WRITE_BITS3(bits);
            dwords -= 2;
        }
        else if (dwords > 0) {
            WRITE_BITS2(bits);
            dwords--;
        }
        else {
            WRITE_BITS1(bits);
        }
#else
        WRITE_BITS(bits);
#endif

        shift += 32;
        shift %= width;
    }
    return base;
}

static CARD32 *
StippleOver32(CARD32 *base, CARD32 *src, int offset, int width, int dwords)
{
    CARD32 *srcp;
    CARD32 bits;
    int bitsleft, shift, usable;

    while (dwords--) {
        bitsleft = width - offset;
        srcp = src + (offset >> 5);
        shift = offset & 31;
        usable = 32 - shift;

        if (bitsleft < 32) {
            if (bitsleft <= usable) {
                bits = SHIFT_L(*src, bitsleft) |
                    (SHIFT_R(*srcp, shift) & XAAShiftMasks[bitsleft]);
            }
            else {
                bits = SHIFT_L(*src, bitsleft) |
                    (SHIFT_L(srcp[1], usable) & XAAShiftMasks[bitsleft]) |
                    (SHIFT_R(*srcp, shift) & XAAShiftMasks[usable]);
            }
        }
        else if (shift)
            bits = SHIFT_R(*srcp, shift) | SHIFT_L(srcp[1], usable);
        else
            bits = *srcp;

#ifdef TRIPLE_BITS
        if (dwords >= 2) {
            WRITE_BITS3(bits);
            dwords -= 2;
        }
        else if (dwords > 0) {
            WRITE_BITS2(bits);
            dwords--;
        }
        else {
            WRITE_BITS1(bits);
        }
#else
        WRITE_BITS(bits);
#endif

        offset += 32;
        offset %= width;
    }
    return base;
}

static CARD32 *
StippleOver32_Inverted(CARD32 *base, CARD32 *src,
                       int offset, int width, int dwords)
{
    CARD32 *srcp;
    CARD32 bits;
    int bitsleft, shift, usable;

    while (dwords--) {
        bitsleft = width - offset;
        srcp = src + (offset >> 5);
        shift = offset & 31;
        usable = 32 - shift;

        if (bitsleft < 32) {
            if (bitsleft <= usable) {
                bits = SHIFT_L(*src, bitsleft) |
                    (SHIFT_R(*srcp, shift) & XAAShiftMasks[bitsleft]);
            }
            else {
                bits = SHIFT_L(*src, bitsleft) |
                    (SHIFT_L(srcp[1], usable) & XAAShiftMasks[bitsleft]) |
                    (SHIFT_R(*srcp, shift) & XAAShiftMasks[usable]);
            }
        }
        else if (shift)
            bits = SHIFT_R(*srcp, shift) | SHIFT_L(srcp[1], usable);
        else
            bits = *srcp;

        bits = ~bits;

#ifdef TRIPLE_BITS
        if (dwords >= 2) {
            WRITE_BITS3(bits);
            dwords -= 2;
        }
        else if (dwords > 0) {
            WRITE_BITS2(bits);
            dwords--;
        }
        else {
            WRITE_BITS1(bits);
        }
#else
        WRITE_BITS(bits);
#endif

        offset += 32;
        offset %= width;
    }
    return base;
}
