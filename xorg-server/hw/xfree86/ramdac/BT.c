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
 * BT RAMDAC routines.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "xf86_OSproc.h"

#define INIT_BT_RAMDAC_INFO
#include "BTPriv.h"
#include "xf86RamDacPriv.h"

void
BTramdacRestore(ScrnInfoPtr pScrn, RamDacRecPtr ramdacPtr,
                RamDacRegRecPtr ramdacReg)
{
    int i;

    /* Here we pass a short, so that we can evaluate a mask too */
    /* So that the mask is the high byte and the data the low byte */
    /* Just the command/status registers */
    for (i = 0x06; i < 0x0A; i++)
        (*ramdacPtr->WriteDAC)
            (pScrn, i, (ramdacReg->DacRegs[i] & 0xFF00) >> 8,
             ramdacReg->DacRegs[i]);
}

void
BTramdacSave(ScrnInfoPtr pScrn, RamDacRecPtr ramdacPtr,
             RamDacRegRecPtr ramdacReg)
{
    int i;

    (*ramdacPtr->ReadAddress) (pScrn, 0);       /* Start at index 0 */
    for (i = 0; i < 768; i++)
        ramdacReg->DAC[i] = (*ramdacPtr->ReadData) (pScrn);

    /* Just the command/status registers */
    for (i = 0x06; i < 0x0A; i++)
        ramdacReg->DacRegs[i] = (*ramdacPtr->ReadDAC) (pScrn, i);
}

RamDacHelperRecPtr
BTramdacProbe(ScrnInfoPtr pScrn,
              RamDacSupportedInfoRecPtr ramdacs /*, RamDacRecPtr ramdacPtr */ )
{
    RamDacRecPtr ramdacPtr = RAMDACSCRPTR(pScrn);
    Bool RamDacIsSupported = FALSE;
    RamDacHelperRecPtr ramdacHelperPtr = NULL;
    int BTramdac_ID = -1;
    int i, status, cmd0;

    /* Save COMMAND Register 0 */
    cmd0 = (*ramdacPtr->ReadDAC) (pScrn, BT_COMMAND_REG_0);
    /* Ensure were going to access the STATUS Register on next read */
    (*ramdacPtr->WriteDAC) (pScrn, BT_COMMAND_REG_0, 0x7F, 0x00);

    status = (*ramdacPtr->ReadDAC) (pScrn, BT_STATUS_REG);
    switch (status) {
    case 0x40:
        BTramdac_ID = ATT20C504_RAMDAC;
        break;
    case 0xD0:
        BTramdac_ID = ATT20C505_RAMDAC;
        break;
    default:
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "Unknown BT RAMDAC type (0x%x), assuming BT485\n", status);
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0x28:                 /* This is for the DEC TGA - Questionable ? */
        BTramdac_ID = BT485_RAMDAC;
        break;
    }

    /* Restore COMMAND Register 0 */
    (*ramdacPtr->WriteDAC) (pScrn, BT_COMMAND_REG_0, 0x00, cmd0);

    if (BTramdac_ID == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "Cannot determine BT RAMDAC type, aborting\n");
        return NULL;
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "Attached RAMDAC is %s\n",
                   BTramdacDeviceInfo[BTramdac_ID & 0xFFFF].DeviceName);
    }

    for (i = 0; ramdacs[i].token != -1; i++) {
        if (ramdacs[i].token == BTramdac_ID)
            RamDacIsSupported = TRUE;
    }

    if (!RamDacIsSupported) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                   "This BT RAMDAC is NOT supported by this driver, aborting\n");
        return NULL;
    }

    ramdacHelperPtr = RamDacHelperCreateInfoRec();
    switch (BTramdac_ID) {
    case BT485_RAMDAC:
        ramdacHelperPtr->SetBpp = BTramdacSetBpp;
        break;
    }
    ramdacPtr->RamDacType = BTramdac_ID;
    ramdacHelperPtr->RamDacType = BTramdac_ID;
    ramdacHelperPtr->Save = BTramdacSave;
    ramdacHelperPtr->Restore = BTramdacRestore;

    return ramdacHelperPtr;
}

void
BTramdacSetBpp(ScrnInfoPtr pScrn, RamDacRegRecPtr ramdacReg)
{
    /* We need to deal with Direct Colour visuals for 8bpp and other
     * good stuff for colours */
    switch (pScrn->bitsPerPixel) {
    case 32:
        ramdacReg->DacRegs[BT_COMMAND_REG_1] = 0x10;
        break;
    case 24:
        ramdacReg->DacRegs[BT_COMMAND_REG_1] = 0x10;
        break;
    case 16:
        ramdacReg->DacRegs[BT_COMMAND_REG_1] = 0x38;
        break;
    case 15:
        ramdacReg->DacRegs[BT_COMMAND_REG_1] = 0x30;
        break;
    case 8:
        ramdacReg->DacRegs[BT_COMMAND_REG_1] = 0x40;
        break;
    case 4:
        ramdacReg->DacRegs[BT_COMMAND_REG_1] = 0x60;
        break;
    }
}
