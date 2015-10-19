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
 * Modified from IBM.c to support TI RAMDAC routines
 *   by Jens Owen, <jens@tungstengraphics.com>.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"

#define INIT_TI_RAMDAC_INFO
#include "TIPriv.h"
#include "xf86RamDacPriv.h"

/* The following values are in kHz */
#define TI_MIN_VCO_FREQ  110000
#define TI_MAX_VCO_FREQ  220000

unsigned long
TIramdacCalculateMNPForClock(unsigned long RefClock,    /* In 100Hz units */
                             unsigned long ReqClock,    /* In 100Hz units */
                             char IsPixClock,   /* boolean, is this the pixel or the sys clock */
                             unsigned long MinClock,    /* Min VCO rating */
                             unsigned long MaxClock,    /* Max VCO rating */
                             unsigned long *rM, /* M Out */
                             unsigned long *rN, /* N Out */
                             unsigned long *rP  /* Min P In, P Out */
    )
{
    unsigned long n, p;
    unsigned long best_m = 0, best_n = 0;
    double VCO, IntRef = (double) RefClock;
    double m_err, inc_m, calc_m;
    unsigned long ActualClock;

    /* Make sure that MinClock <= ReqClock <= MaxClock */
    if (ReqClock < MinClock)
        ReqClock = MinClock;
    if (ReqClock > MaxClock)
        ReqClock = MaxClock;

    /*
     * ActualClock = VCO / 2 ^ p
     * Choose p so that TI_MIN_VCO_FREQ <= VCO <= TI_MAX_VCO_FREQ
     * Note that since TI_MAX_VCO_FREQ = 2 * TI_MIN_VCO_FREQ
     * we don't have to bother checking for this maximum limit.
     */
    VCO = (double) ReqClock;
    for (p = 0; p < 3 && VCO < TI_MIN_VCO_FREQ; (p)++)
        VCO *= 2.0;

    /*
     * We avoid doing multiplications by ( 65 - n ),
     * and add an increment instead - this keeps any error small.
     */
    inc_m = VCO / (IntRef * 8.0);

    /* Initial value of calc_m for the loop */
    calc_m = inc_m + inc_m + inc_m;

    /* Initial amount of error for an integer - impossibly large */
    m_err = 2.0;

    /* Search for the closest INTEGER value of ( 65 - m ) */
    for (n = 3; n <= 25; (n)++, calc_m += inc_m) {

        /* Ignore values of ( 65 - m ) which we can't use */
        if (calc_m < 3.0 || calc_m > 64.0)
            continue;

        /*
         * Pick the closest INTEGER (has smallest fractional part).
         * The optimizer should clean this up for us.
         */
        if ((calc_m - (int) calc_m) < m_err) {
            m_err = calc_m - (int) calc_m;
            best_m = (int) calc_m;
            best_n = n;
        }
    }

    /* 65 - ( 65 - x ) = x */
    *rM = 65 - best_m;
    *rN = 65 - best_n;
    *rP = p;

    /* Now all the calculations can be completed */
    VCO = 8.0 * IntRef * best_m / best_n;
    ActualClock = VCO / (1 << p);

    DebugF("f_out=%ld f_vco=%.1f n=%lu m=%lu p=%lu\n",
           ActualClock, VCO, *rN, *rM, *rP);

    return ActualClock;
}

void
TIramdacRestore(ScrnInfoPtr pScrn, RamDacRecPtr ramdacPtr,
                RamDacRegRecPtr ramdacReg)
{
    int i;
    unsigned long status;

    /* Here we pass a short, so that we can evaluate a mask too
     * So that the mask is the high byte and the data the low byte
     * Order is important
     */
    TIRESTORE(TIDAC_latch_ctrl);
    TIRESTORE(TIDAC_true_color_ctrl);
    TIRESTORE(TIDAC_multiplex_ctrl);
    TIRESTORE(TIDAC_clock_select);
    TIRESTORE(TIDAC_palette_page);
    TIRESTORE(TIDAC_general_ctrl);
    TIRESTORE(TIDAC_misc_ctrl);
    /* 0x2A & 0x2B are reserved */
    TIRESTORE(TIDAC_key_over_low);
    TIRESTORE(TIDAC_key_over_high);
    TIRESTORE(TIDAC_key_red_low);
    TIRESTORE(TIDAC_key_red_high);
    TIRESTORE(TIDAC_key_green_low);
    TIRESTORE(TIDAC_key_green_high);
    TIRESTORE(TIDAC_key_blue_low);
    TIRESTORE(TIDAC_key_blue_high);
    TIRESTORE(TIDAC_key_ctrl);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_clock_ctrl, 0, 0x30);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_clock_ctrl, 0, 0x38);
    TIRESTORE(TIDAC_clock_ctrl);
    TIRESTORE(TIDAC_sense_test);
    TIRESTORE(TIDAC_ind_curs_ctrl);

    /* only restore clocks if they were valid to begin with */

    if (ramdacReg->DacRegs[TIDAC_PIXEL_VALID]) {
        /* Reset pixel clock */
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0x22);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_pixel_data, 0, 0x3c);

        /* Restore N, M & P values for pixel clocks */
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_pixel_data, 0,
                                ramdacReg->DacRegs[TIDAC_PIXEL_N]);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_pixel_data, 0,
                                ramdacReg->DacRegs[TIDAC_PIXEL_M]);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_pixel_data, 0,
                                ramdacReg->DacRegs[TIDAC_PIXEL_P]);

        /* wait for pixel clock to lock */
        i = 1000000;
        do {
            status = (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_pixel_data);
        } while ((!(status & 0x40)) && (--i));
        if (!(status & 0x40)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Pixel clock setup timed out\n");
            return;
        }
    }

    if (ramdacReg->DacRegs[TIDAC_LOOP_VALID]) {
        /* Reset loop clock */
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0x22);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_loop_data, 0, 0x70);

        /* Restore N, M & P values for pixel clocks */
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_loop_data, 0,
                                ramdacReg->DacRegs[TIDAC_LOOP_N]);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_loop_data, 0,
                                ramdacReg->DacRegs[TIDAC_LOOP_M]);
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_loop_data, 0,
                                ramdacReg->DacRegs[TIDAC_LOOP_P]);

        /* wait for loop clock to lock */
        i = 1000000;
        do {
            status = (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_loop_data);
        } while ((!(status & 0x40)) && (--i));
        if (!(status & 0x40)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Loop clock setup timed out\n");
            return;
        }
    }

    /* restore palette */
    (*ramdacPtr->WriteAddress) (pScrn, 0);
#ifndef NOT_DONE
    for (i = 0; i < 768; i++)
        (*ramdacPtr->WriteData) (pScrn, ramdacReg->DAC[i]);
#else
    (*ramdacPtr->WriteData) (pScrn, 0);
    (*ramdacPtr->WriteData) (pScrn, 0);
    (*ramdacPtr->WriteData) (pScrn, 0);
    for (i = 0; i < 765; i++)
        (*ramdacPtr->WriteData) (pScrn, 0xff);
#endif
}

void
TIramdacSave(ScrnInfoPtr pScrn, RamDacRecPtr ramdacPtr,
             RamDacRegRecPtr ramdacReg)
{
    int i;

    (*ramdacPtr->ReadAddress) (pScrn, 0);
    for (i = 0; i < 768; i++)
        ramdacReg->DAC[i] = (*ramdacPtr->ReadData) (pScrn);

    /* Read back N,M and P values for pixel clock */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0);
    ramdacReg->DacRegs[TIDAC_PIXEL_N] =
        (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_pixel_data);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0x11);
    ramdacReg->DacRegs[TIDAC_PIXEL_M] =
        (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_pixel_data);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0x22);
    ramdacReg->DacRegs[TIDAC_PIXEL_P] =
        (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_pixel_data);

    /* Read back N,M and P values for loop clock */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0);
    ramdacReg->DacRegs[TIDAC_LOOP_N] =
        (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_loop_data);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0x11);
    ramdacReg->DacRegs[TIDAC_LOOP_M] =
        (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_loop_data);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_pll_addr, 0, 0x22);
    ramdacReg->DacRegs[TIDAC_LOOP_P] =
        (*ramdacPtr->ReadDAC) (pScrn, TIDAC_pll_loop_data);

    /* Order is important */
    TISAVE(TIDAC_latch_ctrl);
    TISAVE(TIDAC_true_color_ctrl);
    TISAVE(TIDAC_multiplex_ctrl);
    TISAVE(TIDAC_clock_select);
    TISAVE(TIDAC_palette_page);
    TISAVE(TIDAC_general_ctrl);
    TISAVE(TIDAC_misc_ctrl);
    /* 0x2A & 0x2B are reserved */
    TISAVE(TIDAC_key_over_low);
    TISAVE(TIDAC_key_over_high);
    TISAVE(TIDAC_key_red_low);
    TISAVE(TIDAC_key_red_high);
    TISAVE(TIDAC_key_green_low);
    TISAVE(TIDAC_key_green_high);
    TISAVE(TIDAC_key_blue_low);
    TISAVE(TIDAC_key_blue_high);
    TISAVE(TIDAC_key_ctrl);
    TISAVE(TIDAC_clock_ctrl);
    TISAVE(TIDAC_sense_test);
    TISAVE(TIDAC_ind_curs_ctrl);
}

RamDacHelperRecPtr
TIramdacProbe(ScrnInfoPtr pScrn, RamDacSupportedInfoRecPtr ramdacs)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);
    RamDacHelperRecPtr ramdacHelperPtr = NULL;
    Bool RamDacIsSupported = FALSE;
    int TIramdac_ID = -1;
    int i;
    unsigned char id, rev, rev2, id2;

    /* read ID and revision */
    rev = (*ramdacPtr->ReadDAC) (pScrn, TIDAC_rev);
    id = (*ramdacPtr->ReadDAC) (pScrn, TIDAC_id);

    /* check if ID and revision are read only */
    (*ramdacPtr->WriteDAC) (pScrn, ~rev, 0, TIDAC_rev);
    (*ramdacPtr->WriteDAC) (pScrn, ~id, 0, TIDAC_id);
    rev2 = (*ramdacPtr->ReadDAC) (pScrn, TIDAC_rev);
    id2 = (*ramdacPtr->ReadDAC) (pScrn, TIDAC_id);

    switch (id) {
    case TIDAC_TVP_3030_ID:
        if (id == id2 && rev == rev2)   /* check for READ ONLY */
            TIramdac_ID = TI3030_RAMDAC;
        break;
    case TIDAC_TVP_3026_ID:
        if (id == id2 && rev == rev2)   /* check for READ ONLY */
            TIramdac_ID = TI3026_RAMDAC;
        break;
    }

    (*ramdacPtr->WriteDAC) (pScrn, rev, 0, TIDAC_rev);
    (*ramdacPtr->WriteDAC) (pScrn, id, 0, TIDAC_id);

    if (TIramdac_ID == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "Cannot determine TI RAMDAC type, aborting\n");
        return NULL;
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "Attached RAMDAC is %s\n",
                   TIramdacDeviceInfo[TIramdac_ID & 0xFFFF].DeviceName);
    }

    for (i = 0; ramdacs[i].token != -1; i++) {
        if (ramdacs[i].token == TIramdac_ID)
            RamDacIsSupported = TRUE;
    }

    if (!RamDacIsSupported) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "This TI RAMDAC is NOT supported by this driver, aborting\n");
        return NULL;
    }

    ramdacHelperPtr = RamDacHelperCreateInfoRec();
    switch (TIramdac_ID) {
    case TI3030_RAMDAC:
        ramdacHelperPtr->SetBpp = TIramdac3030SetBpp;
        ramdacHelperPtr->HWCursorInit = TIramdacHWCursorInit;
        break;
    case TI3026_RAMDAC:
        ramdacHelperPtr->SetBpp = TIramdac3026SetBpp;
        ramdacHelperPtr->HWCursorInit = TIramdacHWCursorInit;
        break;
    }
    ramdacPtr->RamDacType = TIramdac_ID;
    ramdacHelperPtr->RamDacType = TIramdac_ID;
    ramdacHelperPtr->Save = TIramdacSave;
    ramdacHelperPtr->Restore = TIramdacRestore;

    return ramdacHelperPtr;
}

void
TIramdac3026SetBpp(ScrnInfoPtr pScrn, RamDacRegRecPtr ramdacReg)
{
    switch (pScrn->bitsPerPixel) {
    case 32:
        /* order is important */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
        ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x46;
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x5c;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x05;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x3C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        if (pScrn->overlayFlags & OVERLAY_8_32_PLANAR) {
            ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x06;
            ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x3C;
            ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x01;
        }
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    case 24:
        /* order is important */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
        ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x56;
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x58;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x25;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x00;
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x2C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    case 16:
        /* order is important */
#if 0
        /* Matrox driver uses this */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x07;
#else
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
#endif
        if (pScrn->depth == 16) {
            ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x45;
        }
        else {
            ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x44;
        }
#if 0
        /* Matrox driver uses this */
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x50;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x15;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x00;
#else
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x54;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x05;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x10;
#endif
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x2C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    case 8:
        /* order is important */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
        ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x80;
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x4c;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x05;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x1C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x00;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    }
}

void
TIramdac3030SetBpp(ScrnInfoPtr pScrn, RamDacRegRecPtr ramdacReg)
{
    switch (pScrn->bitsPerPixel) {
    case 32:
        /* order is important */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
        ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x46;
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x5D;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x05;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x3C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        if (pScrn->overlayFlags & OVERLAY_8_32_PLANAR) {
            ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x06;
            ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x3C;
            ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x01;
        }
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    case 24:
        /* order is important */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
        ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x56;
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x58;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x25;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x00;
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x2C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    case 16:
        /* order is important */
#if 0
        /* Matrox driver uses this */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x07;
#else
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
#endif
        if (pScrn->depth == 16) {
            ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x45;
        }
        else {
            ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x44;
        }
#if 0
        /* Matrox driver uses this */
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x50;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x15;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x00;
#else
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x55;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x85;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x10;
#endif
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x2C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    case 8:
        /* order is important */
        ramdacReg->DacRegs[TIDAC_latch_ctrl] = 0x06;
        ramdacReg->DacRegs[TIDAC_true_color_ctrl] = 0x80;
        ramdacReg->DacRegs[TIDAC_multiplex_ctrl] = 0x4d;
        ramdacReg->DacRegs[TIDAC_clock_select] = 0x05;
        ramdacReg->DacRegs[TIDAC_palette_page] = 0x00;
        ramdacReg->DacRegs[TIDAC_general_ctrl] = 0x10;
        ramdacReg->DacRegs[TIDAC_misc_ctrl] = 0x1C;
        /* 0x2A & 0x2B are reserved */
        ramdacReg->DacRegs[TIDAC_key_over_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_over_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_red_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_low] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_green_high] = 0xFF;
        ramdacReg->DacRegs[TIDAC_key_blue_low] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_blue_high] = 0x00;
        ramdacReg->DacRegs[TIDAC_key_ctrl] = 0x00;
        ramdacReg->DacRegs[TIDAC_sense_test] = 0x00;
        ramdacReg->DacRegs[TIDAC_ind_curs_ctrl] = 0x00;
        break;
    }
}

static void
TIramdacShowCursor(ScrnInfoPtr pScrn)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    /* Enable cursor - X11 mode */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_ind_curs_ctrl, 0, 0x03);
}

static void
TIramdacHideCursor(ScrnInfoPtr pScrn)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    /* Disable cursor - X11 mode */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_ind_curs_ctrl, 0, 0x00);
}

static void
TIramdacSetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    x += 64;
    y += 64;

    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_XLOW, 0, x & 0xff);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_XHIGH, 0, (x >> 8) & 0x0f);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_YLOW, 0, y & 0xff);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_YHIGH, 0, (y >> 8) & 0x0f);
}

static void
TIramdacSetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);

    /* Background color */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_WRITE_ADDR, 0, 1);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_COLOR, 0,
                            ((bg & 0x00ff0000) >> 16));
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_COLOR, 0,
                            ((bg & 0x0000ff00) >> 8));
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_COLOR, 0, (bg & 0x000000ff));

    /* Foreground color */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_WRITE_ADDR, 0, 2);
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_COLOR, 0,
                            ((fg & 0x00ff0000) >> 16));
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_COLOR, 0,
                            ((fg & 0x0000ff00) >> 8));
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_COLOR, 0, (fg & 0x000000ff));
}

static Bool
TIramdacLoadCursorImage(ScrnInfoPtr pScrn, unsigned char *src)
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);
    int i = 1024;

    /* reset A9,A8 */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_ind_curs_ctrl, 0, 0x00);
    /* reset cursor RAM load address A7..A0 */
    (*ramdacPtr->WriteDAC) (pScrn, TIDAC_INDEX, 0x00, 0x00);

    while (i--) {
        /* NOT_DONE: might need a delay here */
        (*ramdacPtr->WriteDAC) (pScrn, TIDAC_CURS_RAM_DATA, 0, *(src++));
    }
    return TRUE;
}

static Bool
TIramdacUseHWCursor(ScreenPtr pScr, CursorPtr pCurs)
{
    return TRUE;
}

void
TIramdacHWCursorInit(xf86CursorInfoPtr infoPtr)
{
    infoPtr->MaxWidth = 64;
    infoPtr->MaxHeight = 64;
    infoPtr->Flags = HARDWARE_CURSOR_BIT_ORDER_MSBFIRST |
        HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
        HARDWARE_CURSOR_SOURCE_MASK_NOT_INTERLEAVED;
    infoPtr->SetCursorColors = TIramdacSetCursorColors;
    infoPtr->SetCursorPosition = TIramdacSetCursorPosition;
    infoPtr->LoadCursorImageCheck = TIramdacLoadCursorImage;
    infoPtr->HideCursor = TIramdacHideCursor;
    infoPtr->ShowCursor = TIramdacShowCursor;
    infoPtr->UseHWCursor = TIramdacUseHWCursor;
}

void
TIramdacLoadPalette(ScrnInfoPtr pScrn,
                    int numColors,
                    int *indices, LOCO * colors, VisualPtr pVisual)
{
    RamDacRecPtr hwp = RAMDACSCRPTR(pScrn);
    int i, index, shift;

    if (pScrn->depth == 16) {
        for (i = 0; i < numColors; i++) {
            index = indices[i];
            (*hwp->WriteAddress) (pScrn, index << 2);
            (*hwp->WriteData) (pScrn, colors[index >> 1].red);
            (*hwp->WriteData) (pScrn, colors[index].green);
            (*hwp->WriteData) (pScrn, colors[index >> 1].blue);

            if (index <= 31) {
                (*hwp->WriteAddress) (pScrn, index << 3);
                (*hwp->WriteData) (pScrn, colors[index].red);
                (*hwp->WriteData) (pScrn, colors[(index << 1) + 1].green);
                (*hwp->WriteData) (pScrn, colors[index].blue);
            }
        }
    }
    else {
        shift = (pScrn->depth == 15) ? 3 : 0;

        for (i = 0; i < numColors; i++) {
            index = indices[i];
            (*hwp->WriteAddress) (pScrn, index << shift);
            (*hwp->WriteData) (pScrn, colors[index].red);
            (*hwp->WriteData) (pScrn, colors[index].green);
            (*hwp->WriteData) (pScrn, colors[index].blue);
        }
    }
}

TIramdacLoadPaletteProc *
TIramdacLoadPaletteWeak(void)
{
    return TIramdacLoadPalette;
}
