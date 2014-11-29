/*
 * Copyright 1998 by Alan Hourihane, Wigan, England.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Alan Hourihane not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Alan Hourihane makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * ALAN HOURIHANE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ALAN HOURIHANE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *
 * IBM RAMDAC routines.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"

#define INIT_IBM_RAMDAC_INFO
#include "IBMPriv.h"
#include "xf86RamDacPriv.h"

#define INITIALFREQERR 100000

unsigned long
IBMramdac640CalculateMNPCForClock(unsigned long RefClock,       /* In 100Hz units */
                                  unsigned long ReqClock,       /* In 100Hz units */
                                  char IsPixClock,      /* boolean, is this the pixel or the sys clock */
                                  unsigned long MinClock,       /* Min VCO rating */
                                  unsigned long MaxClock,       /* Max VCO rating */
                                  unsigned long *rM,    /* M Out */
                                  unsigned long *rN,    /* N Out */
                                  unsigned long *rP,    /* Min P In, P Out */
                                  unsigned long *rC     /* C Out */
    )
{
    unsigned long M, N, P, iP = *rP;
    unsigned long IntRef, VCO, Clock;
    long freqErr, lowestFreqErr = INITIALFREQERR;
    unsigned long ActualClock = 0;

    for (N = 0; N <= 63; N++) {
        IntRef = RefClock / (N + 1);
        if (IntRef < 10000)
            break;              /* IntRef needs to be >= 1MHz */
        for (M = 2; M <= 127; M++) {
            VCO = IntRef * (M + 1);
            if ((VCO < MinClock) || (VCO > MaxClock))
                continue;
            for (P = iP; P <= 4; P++) {
                if (P != 0)
                    Clock = (RefClock * (M + 1)) / ((N + 1) * 2 * P);
                else
                    Clock = (RefClock * (M + 1)) / (N + 1);

                freqErr = (Clock - ReqClock);

                if (freqErr < 0) {
                    /* PixelClock gets rounded up always so monitor reports
                       correct frequency. */
                    if (IsPixClock)
                        continue;
                    freqErr = -freqErr;
                }

                if (freqErr < lowestFreqErr) {
                    *rM = M;
                    *rN = N;
                    *rP = P;
                    *rC = (VCO <= 1280000 ? 1 : 2);
                    ActualClock = Clock;

                    lowestFreqErr = freqErr;
                    /* Return if we found an exact match */
                    if (freqErr == 0)
                        return ActualClock;
                }
            }
        }
    }

    return ActualClock;
}

unsigned long
IBMramdac526CalculateMNPCForClock(unsigned long RefClock,       /* In 100Hz units */
                                  unsigned long ReqClock,       /* In 100Hz units */
                                  char IsPixClock,      /* boolean, is this the pixel or the sys clock */
                                  unsigned long MinClock,       /* Min VCO rating */
                                  unsigned long MaxClock,       /* Max VCO rating */
                                  unsigned long *rM,    /* M Out */
                                  unsigned long *rN,    /* N Out */
                                  unsigned long *rP,    /* Min P In, P Out */
                                  unsigned long *rC     /* C Out */
    )
{
    unsigned long M, N, P, iP = *rP;
    unsigned long IntRef, VCO, Clock;
    long freqErr, lowestFreqErr = INITIALFREQERR;
    unsigned long ActualClock = 0;

    for (N = 0; N <= 63; N++) {
        IntRef = RefClock / (N + 1);
        if (IntRef < 10000)
            break;              /* IntRef needs to be >= 1MHz */
        for (M = 0; M <= 63; M++) {
            VCO = IntRef * (M + 1);
            if ((VCO < MinClock) || (VCO > MaxClock))
                continue;
            for (P = iP; P <= 4; P++) {
                if (P)
                    Clock = (RefClock * (M + 1)) / ((N + 1) * 2 * P);
                else
                    Clock = VCO;

                freqErr = (Clock - ReqClock);

                if (freqErr < 0) {
                    /* PixelClock gets rounded up always so monitor reports
                       correct frequency. */
                    if (IsPixClock)
                        continue;
                    freqErr = -freqErr;
                }

                if (freqErr < lowestFreqErr) {
                    *rM = M;
                    *rN = N;
                    *rP = P;
                    *rC = (VCO <= 1280000 ? 1 : 2);
                    ActualClock = Clock;

                    lowestFreqErr = freqErr;
                    /* Return if we found an exact match */
                    if (freqErr == 0)
                        return ActualClock;
                }
            }
        }
    }

    return ActualClock;
}

void
IBMramdacRestore(ScrnInfoPtr pScrn, RamDacRecPtr ramdacPtr,
                 RamDacRegRecPtr ramdacReg)
{
    int i, maxreg, dacreg;

    switch (ramdacPtr->RamDacType) {
    case IBM640_RAMDAC:
        maxreg = 0x300;
        dacreg = 1024;
        break;
    default:
        maxreg = 0x100;
        dacreg = 768;
        break;
    }

    /* Here we pass a short, so that we can evaluate a mask too */
    /* So that the mask is the high byte and the data the low byte */
    for (i = 0; i < maxreg; i++)
        (*ramdacPtr->WriteDAC)
            (pScrn, i, (ramdacReg->DacRegs[i] & 0xFF00) >> 8,
             ramdacReg->DacRegs[i]);

    (*ramdacPtr->WriteAddress) (pScrn, 0);
    for (i = 0; i < dacreg; i++)
        (*ramdacPtr->WriteData) (pScrn, ramdacReg->DAC[i]);
}

void
IBMramdacSave(ScrnInfoPtr pScrn, RamDacRecPtr ramdacPtr,
              RamDacRegRecPtr ramdacReg)
{
    int i, maxreg, dacreg;

    switch (ramdacPtr->RamDacType) {
    case IBM640_RAMDAC:
        maxreg = 0x300;
        dacreg = 1024;
        break;
    default:
        maxreg = 0x100;
        dacreg = 768;
        break;
    }

    (*ramdacPtr->ReadAddress) (pScrn, 0);
    for (i = 0; i < dacreg; i++)
        ramdacReg->DAC[i] = (*ramdacPtr->ReadData) (pScrn);

    for (i = 0; i < maxreg; i++)
        ramdacReg->DacRegs[i] = (*ramdacPtr->ReadDAC) (pScrn, i);
}

RamDacHelperRecPtr
IBMramdacProbe(ScrnInfoPtr pScrn,
               RamDacSupportedInfoRecPtr ramdacs /* , RamDacRecPtr ramdacPtr */
               )
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);
    RamDacHelperRecPtr ramdacHelperPtr = NULL;
    Bool RamDacIsSupported = FALSE;
    int IBMramdac_ID = -1;
    int i;
    unsigned char id, rev, id2, rev2;

    /* read ID and revision */
    rev = (*ramdacPtr->ReadDAC) (pScrn, IBMRGB_rev);
    id = (*ramdacPtr->ReadDAC) (pScrn, IBMRGB_id);

    /* check if ID and revision are read only */
    (*ramdacPtr->WriteDAC) (pScrn, ~rev, 0, IBMRGB_rev);
    (*ramdacPtr->WriteDAC) (pScrn, ~id, 0, IBMRGB_id);
    rev2 = (*ramdacPtr->ReadDAC) (pScrn, IBMRGB_rev);
    id2 = (*ramdacPtr->ReadDAC) (pScrn, IBMRGB_id);

    switch (id) {
    case 0x30:
        if (rev == 0xc0)
            IBMramdac_ID = IBM624_RAMDAC;
        if (rev == 0x80)
            IBMramdac_ID = IBM624DB_RAMDAC;
        break;
    case 0x12:
        if (rev == 0x1c)
            IBMramdac_ID = IBM640_RAMDAC;
        break;
    case 0x01:
        IBMramdac_ID = IBM525_RAMDAC;
        break;
    case 0x02:
        if (rev == 0xf0)
            IBMramdac_ID = IBM524_RAMDAC;
        if (rev == 0xe0)
            IBMramdac_ID = IBM524A_RAMDAC;
        if (rev == 0xc0)
            IBMramdac_ID = IBM526_RAMDAC;
        if (rev == 0x80)
            IBMramdac_ID = IBM526DB_RAMDAC;
        break;
    }

    if (id == 1 || id == 2) {
        if (id == id2 && rev == rev2) { /* IBM RGB52x found */
            /* check for 128bit VRAM -> RGB528 */
            if (((*ramdacPtr->ReadDAC) (pScrn, IBMRGB_misc1) & 0x03) == 0x03) {
                IBMramdac_ID = IBM528_RAMDAC;   /* 128bit DAC found */
                if (rev == 0xe0)
                    IBMramdac_ID = IBM528A_RAMDAC;
            }
        }
    }

    (*ramdacPtr->WriteDAC) (pScrn, rev, 0, IBMRGB_rev);
    (*ramdacPtr->WriteDAC) (pScrn, id, 0, IBMRGB_id);

    if (IBMramdac_ID == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "Cannot determine IBM RAMDAC type, aborting\n");
        return NULL;
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "Attached RAMDAC is %s\n",
                   IBMramdacDeviceInfo[IBMramdac_ID & 0xFFFF].DeviceName);
    }

    for (i = 0; ramdacs[i].token != -1; i++) {
        if (ramdacs[i].token == IBMramdac_ID)
            RamDacIsSupported = TRUE;
    }

    if (!RamDacIsSupported) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "This IBM RAMDAC is NOT supported by this driver, aborting\n");
        return NULL;
    }

    ramdacHelperPtr = RamDacHelperCreateInfoRec();
    switch (IBMramdac_ID) {
    case IBM526_RAMDAC:
    case IBM526DB_RAMDAC:
        ramdacHelperPtr->SetBpp = IBMramdac526SetBpp;
        ramdacHelperPtr->HWCursorInit = IBMramdac526HWCursorInit;
        break;
    case IBM640_RAMDAC:
        ramdacHelperPtr->SetBpp = IBMramdac640SetBpp;
        ramdacHelperPtr->HWCursorInit = IBMramdac640HWCursorInit;
        break;
    }
    ramdacPtr->RamDacType = IBMramdac_ID;
    ramdacHelperPtr->RamDacType = IBMramdac_ID;
    ramdacHelperPtr->Save = IBMramdacSave;
    ramdacHelperPtr->Restore = IBMramdacRestore;

    return ramdacHelperPtr;
}

void
IBMramdac526SetBpp(ScrnInfoPtr pScrn, RamDacRegRecPtr ramdacReg)
{
    ramdacReg->DacRegs[IBMRGB_key_control] = 0x00;      /* Disable Chroma Key */

    switch (pScrn->bitsPerPixel) {
    case 32:
        ramdacReg->DacRegs[IBMRGB_pix_fmt] = PIXEL_FORMAT_32BPP;
        ramdacReg->DacRegs[IBMRGB_32bpp] = B32_DCOL_DIRECT;
        ramdacReg->DacRegs[IBMRGB_24bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_16bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_8bpp] = 0;
        if (pScrn->overlayFlags & OVERLAY_8_32_PLANAR) {
            ramdacReg->DacRegs[IBMRGB_key_control] = 0x01;      /* Enable Key */
            ramdacReg->DacRegs[IBMRGB_key] = 0xFF;
            ramdacReg->DacRegs[IBMRGB_key_mask] = 0xFF;
        }
        break;
    case 24:
        ramdacReg->DacRegs[IBMRGB_pix_fmt] = PIXEL_FORMAT_24BPP;
        ramdacReg->DacRegs[IBMRGB_32bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_24bpp] = B24_DCOL_DIRECT;
        ramdacReg->DacRegs[IBMRGB_16bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_8bpp] = 0;
        break;
    case 16:
        if (pScrn->depth == 16) {
            ramdacReg->DacRegs[IBMRGB_pix_fmt] = PIXEL_FORMAT_16BPP;
            ramdacReg->DacRegs[IBMRGB_32bpp] = 0;
            ramdacReg->DacRegs[IBMRGB_24bpp] = 0;
            ramdacReg->DacRegs[IBMRGB_16bpp] = B16_DCOL_DIRECT | B16_LINEAR |
                B16_CONTIGUOUS | B16_565;
            ramdacReg->DacRegs[IBMRGB_8bpp] = 0;
        }
        else {
            ramdacReg->DacRegs[IBMRGB_pix_fmt] = PIXEL_FORMAT_16BPP;
            ramdacReg->DacRegs[IBMRGB_32bpp] = 0;
            ramdacReg->DacRegs[IBMRGB_24bpp] = 0;
            ramdacReg->DacRegs[IBMRGB_16bpp] = B16_DCOL_DIRECT | B16_LINEAR |
                B16_CONTIGUOUS | B16_555;
            ramdacReg->DacRegs[IBMRGB_8bpp] = 0;
        }
        break;
    case 8:
        ramdacReg->DacRegs[IBMRGB_pix_fmt] = PIXEL_FORMAT_8BPP;
        ramdacReg->DacRegs[IBMRGB_32bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_24bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_16bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_8bpp] = B8_DCOL_INDIRECT;
        break;
    case 4:
        ramdacReg->DacRegs[IBMRGB_pix_fmt] = PIXEL_FORMAT_4BPP;
        ramdacReg->DacRegs[IBMRGB_32bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_24bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_16bpp] = 0;
        ramdacReg->DacRegs[IBMRGB_8bpp] = 0;
    }
}

IBMramdac526SetBppProc *
IBMramdac526SetBppWeak(void)
{
    return IBMramdac526SetBpp;
}

void
IBMramdac640SetBpp(ScrnInfoPtr pScrn, RamDacRegRecPtr ramdacReg)
{
    unsigned char bpp = 0x00;
    unsigned char overlaybpp = 0x00;
    unsigned char offset = 0x00;
    unsigned char dispcont = 0x44;

    ramdacReg->DacRegs[RGB640_SER_WID_03_00] = 0x00;
    ramdacReg->DacRegs[RGB640_SER_WID_07_04] = 0x00;
    ramdacReg->DacRegs[RGB640_DIAGS] = 0x07;

    switch (pScrn->depth) {
    case 8:
        ramdacReg->DacRegs[RGB640_SER_07_00] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_15_08] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_23_16] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_31_24] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_MODE] = IBM640_SER_16_1;  /*16:1 Mux */
        ramdacReg->DacRegs[RGB640_MISC_CONF] = IBM640_PCLK_8;   /* pll / 8 */
        bpp = 0x03;
        break;
    case 15:
        ramdacReg->DacRegs[RGB640_SER_07_00] = 0x10;
        ramdacReg->DacRegs[RGB640_SER_15_08] = 0x11;
        ramdacReg->DacRegs[RGB640_SER_23_16] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_31_24] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_MODE] = IBM640_SER_8_1;   /* 8:1 Mux */
        ramdacReg->DacRegs[RGB640_MISC_CONF] = IBM640_PCLK_8;   /* pll / 8 */
        bpp = 0x0E;
        break;
    case 16:
        ramdacReg->DacRegs[RGB640_SER_07_00] = 0x10;
        ramdacReg->DacRegs[RGB640_SER_15_08] = 0x11;
        ramdacReg->DacRegs[RGB640_SER_23_16] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_31_24] = 0x00;
        ramdacReg->DacRegs[RGB640_SER_MODE] = IBM640_SER_8_1;   /* 8:1 Mux */
        ramdacReg->DacRegs[RGB640_MISC_CONF] = IBM640_PCLK_8;   /* pll / 8 */
        bpp = 0x05;
        break;
    case 24:
        ramdacReg->DacRegs[RGB640_SER_07_00] = 0x30;
        ramdacReg->DacRegs[RGB640_SER_15_08] = 0x31;
        ramdacReg->DacRegs[RGB640_SER_23_16] = 0x32;
        ramdacReg->DacRegs[RGB640_SER_31_24] = 0x33;
        ramdacReg->DacRegs[RGB640_SER_MODE] = IBM640_SER_4_1;   /* 4:1 Mux */
        ramdacReg->DacRegs[RGB640_MISC_CONF] = IBM640_PCLK_8;   /* pll / 8 */
        bpp = 0x09;
        if (pScrn->overlayFlags & OVERLAY_8_32_PLANAR) {
            ramdacReg->DacRegs[RGB640_SER_WID_07_04] = 0x04;
            ramdacReg->DacRegs[RGB640_CHROMA_KEY0] = 0xFF;
            ramdacReg->DacRegs[RGB640_CHROMA_MASK0] = 0xFF;
            offset = 0x04;
            overlaybpp = 0x04;
            dispcont = 0x48;
        }
        break;
    case 30:                   /* 10 bit dac */
        ramdacReg->DacRegs[RGB640_SER_07_00] = 0x30;
        ramdacReg->DacRegs[RGB640_SER_15_08] = 0x31;
        ramdacReg->DacRegs[RGB640_SER_23_16] = 0x32;
        ramdacReg->DacRegs[RGB640_SER_31_24] = 0x33;
        ramdacReg->DacRegs[RGB640_SER_MODE] = IBM640_SER_4_1;   /* 4:1 Mux */
        ramdacReg->DacRegs[RGB640_MISC_CONF] = IBM640_PSIZE10 | IBM640_PCLK_8;  /* pll / 8 */
        bpp = 0x0D;
        break;
    }

    {
        int i;

        for (i = 0x100; i < 0x140; i += 4) {
            /* Initialize FrameBuffer Window Attribute Table */
            ramdacReg->DacRegs[i + 0] = bpp;
            ramdacReg->DacRegs[i + 1] = offset;
            ramdacReg->DacRegs[i + 2] = 0x00;
            ramdacReg->DacRegs[i + 3] = 0x00;
            /* Initialize Overlay Window Attribute Table */
            ramdacReg->DacRegs[i + 0x100] = overlaybpp;
            ramdacReg->DacRegs[i + 0x101] = 0x00;
            ramdacReg->DacRegs[i + 0x102] = 0x00;
            ramdacReg->DacRegs[i + 0x103] = dispcont;
        }
    }
}

static void
IBMramdac526ShowCursor(ScrnInfoPtr pScrn)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    /* Enable cursor - X11 mode */
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs, 0x00, 0x07);
}

static void
IBMramdac640ShowCursor(ScrnInfoPtr pScrn)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    /* Enable cursor - mode2 (x11 mode) */
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURSOR_CONTROL, 0x00, 0x0B);
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CROSSHAIR_CONTROL, 0x00, 0x00);
}

static void
IBMramdac526HideCursor(ScrnInfoPtr pScrn)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    /* Disable cursor - X11 mode */
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs, 0x00, 0x24);
}

static void
IBMramdac640HideCursor(ScrnInfoPtr pScrn)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    /* Disable cursor - mode2 (x11 mode) */
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURSOR_CONTROL, 0x00, 0x08);
}

static void
IBMramdac526SetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    x += 64;
    y += 64;

    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_hot_x, 0x00, 0x3f);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_hot_y, 0x00, 0x3f);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_xl, 0x00, x & 0xff);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_xh, 0x00, (x >> 8) & 0xf);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_yl, 0x00, y & 0xff);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_yh, 0x00, (y >> 8) & 0xf);
}

static void
IBMramdac640SetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    x += 64;
    y += 64;

    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_OFFSETX, 0x00, 0x3f);
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_OFFSETY, 0x00, 0x3f);
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_X_LOW, 0x00, x & 0xff);
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_X_HIGH, 0x00, (x >> 8) & 0xf);
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_Y_LOW, 0x00, y & 0xff);
    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_Y_HIGH, 0x00, (y >> 8) & 0xf);
}

static void
IBMramdac526SetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_col1_r, 0x00, bg >> 16);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_col1_g, 0x00, bg >> 8);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_col1_b, 0x00, bg);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_col2_r, 0x00, fg >> 16);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_col2_g, 0x00, fg >> 8);
    (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_col2_b, 0x00, fg);
}

static void
IBMramdac640SetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_COL0, 0x00, 0);
    (*ramdacPtr->WriteData) (pScrn, fg >> 16);
    (*ramdacPtr->WriteData) (pScrn, fg >> 8);
    (*ramdacPtr->WriteData) (pScrn, fg);
    (*ramdacPtr->WriteData) (pScrn, bg >> 16);
    (*ramdacPtr->WriteData) (pScrn, bg >> 8);
    (*ramdacPtr->WriteData) (pScrn, bg);
    (*ramdacPtr->WriteData) (pScrn, fg >> 16);
    (*ramdacPtr->WriteData) (pScrn, fg >> 8);
    (*ramdacPtr->WriteData) (pScrn, fg);
    (*ramdacPtr->WriteData) (pScrn, bg >> 16);
    (*ramdacPtr->WriteData) (pScrn, bg >> 8);
    (*ramdacPtr->WriteData) (pScrn, bg);
}

static Bool
IBMramdac526LoadCursorImage(ScrnInfoPtr pScrn, unsigned char *src)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);
    int i;

    /*
     * Output the cursor data.  The realize function has put the planes into
     * their correct order, so we can just blast this out.
     */
    for (i = 0; i < 1024; i++)
        (*ramdacPtr->WriteDAC) (pScrn, IBMRGB_curs_array + i, 0x00, (*src++));
    return TRUE;
}

static Bool
IBMramdac640LoadCursorImage(ScrnInfoPtr pScrn, unsigned char *src)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);
    int i;

    /*
     * Output the cursor data.  The realize function has put the planes into
     * their correct order, so we can just blast this out.
     */
    for (i = 0; i < 1024; i++)
        (*ramdacPtr->WriteDAC) (pScrn, RGB640_CURS_WRITE + i, 0x00, (*src++));
    return TRUE;
}

static Bool
IBMramdac526UseHWCursor(ScreenPtr pScr, CursorPtr pCurs)
{
    return TRUE;
}

static Bool
IBMramdac640UseHWCursor(ScreenPtr pScr, CursorPtr pCurs)
{
    return TRUE;
}

void
IBMramdac526HWCursorInit(xf86CursorInfoPtr infoPtr)
{
    infoPtr->MaxWidth = 64;
    infoPtr->MaxHeight = 64;
    infoPtr->Flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
        HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
        HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1;
    infoPtr->SetCursorColors = IBMramdac526SetCursorColors;
    infoPtr->SetCursorPosition = IBMramdac526SetCursorPosition;
    infoPtr->LoadCursorImageCheck = IBMramdac526LoadCursorImage;
    infoPtr->HideCursor = IBMramdac526HideCursor;
    infoPtr->ShowCursor = IBMramdac526ShowCursor;
    infoPtr->UseHWCursor = IBMramdac526UseHWCursor;
}

void
IBMramdac640HWCursorInit(xf86CursorInfoPtr infoPtr)
{
    infoPtr->MaxWidth = 64;
    infoPtr->MaxHeight = 64;
    infoPtr->Flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
        HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
        HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1;
    infoPtr->SetCursorColors = IBMramdac640SetCursorColors;
    infoPtr->SetCursorPosition = IBMramdac640SetCursorPosition;
    infoPtr->LoadCursorImageCheck = IBMramdac640LoadCursorImage;
    infoPtr->HideCursor = IBMramdac640HideCursor;
    infoPtr->ShowCursor = IBMramdac640ShowCursor;
    infoPtr->UseHWCursor = IBMramdac640UseHWCursor;
}
