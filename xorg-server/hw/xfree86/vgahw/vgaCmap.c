/*
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Thomas Roell not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Thomas Roell makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THOMAS ROELL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THOMAS ROELL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include <X11/Xproto.h>
#include "windowstr.h"
#include "compiler.h"
#include "mipointer.h"
#include "micmap.h"

#include "xf86.h"
#include "vgaHW.h"

#include <X11/extensions/xf86dgaproto.h>
#include "dgaproc.h"

#define NOMAPYET        (ColormapPtr) 0

int
vgaListInstalledColormaps(pScreen, pmaps)
ScreenPtr pScreen;
Colormap *pmaps;
{
    /* By the time we are processing requests, we can guarantee that there
     * is always a colormap installed */

    *pmaps = GetInstalledmiColormap(pScreen)->mid;
    return 1;
}

int
vgaGetInstalledColormaps(pScreen, pmaps)
ScreenPtr pScreen;
ColormapPtr *pmaps;
{
    /* By the time we are processing requests, we can guarantee that there
     * is always a colormap installed */

    *pmaps = GetInstalledmiColormap(pScreen);
    return 1;
}

int
vgaCheckColorMap(ColormapPtr pmap)
{
    return (pmap != GetInstalledmiColormap(pmap->pScreen));
}

void
vgaStoreColors(pmap, ndef, pdefs)
ColormapPtr pmap;
int ndef;
xColorItem *pdefs;
{
    int i;
    unsigned char *cmap, *tmp = NULL;
    xColorItem directDefs[256];
    Bool new_overscan = FALSE;
    Bool writeColormap;
    int scrnIndex = pmap->pScreen->myNum;
    ScrnInfoPtr scrninfp = xf86ScreenToScrn(pmap->pScreen);
    vgaHWPtr hwp = VGAHWPTR(scrninfp);

    unsigned char overscan = hwp->ModeReg.Attribute[OVERSCAN];
    unsigned char tmp_overscan = 0;

    if (vgaCheckColorMap(pmap))
        return;

    if ((pmap->pVisual->class | DynamicClass) == DirectColor) {
        ndef = miExpandDirectColors(pmap, ndef, pdefs, directDefs);
        pdefs = directDefs;
    }

    writeColormap = scrninfp->vtSema;
    if (DGAScreenAvailable(pmap->pScreen)) {
        writeColormap = writeColormap ||
            (DGAGetDirectMode(scrnIndex) &&
             !(DGAGetFlags(scrnIndex) & XF86DGADirectColormap)) ||
            (DGAGetFlags(scrnIndex) & XF86DGAHasColormap);
    }

    if (writeColormap)
        hwp->enablePalette(hwp);

    for (i = 0; i < ndef; i++) {
        if (pdefs[i].pixel == overscan) {
            new_overscan = TRUE;
        }
        cmap = &(hwp->ModeReg.DAC[pdefs[i].pixel * 3]);
        if (scrninfp->rgbBits == 8) {
            cmap[0] = pdefs[i].red >> 8;
            cmap[1] = pdefs[i].green >> 8;
            cmap[2] = pdefs[i].blue >> 8;
        }
        else {
            cmap[0] = pdefs[i].red >> 10;
            cmap[1] = pdefs[i].green >> 10;
            cmap[2] = pdefs[i].blue >> 10;
        }
#if 0
        if (clgd6225Lcd) {
            /* The LCD doesn't like white */
            if (cmap[0] == 63)
                cmap[0] = 62;
            if (cmap[1] == 63)
                cmap[1] = 62;
            if (cmap[2] == 63)
                cmap[2] = 62;
        }
#endif

        if (writeColormap) {
            if (hwp->ShowOverscan && i == 255)
                continue;
            hwp->writeDacWriteAddr(hwp, pdefs[i].pixel);
            DACDelay(hwp);
            hwp->writeDacData(hwp, cmap[0]);
            DACDelay(hwp);
            hwp->writeDacData(hwp, cmap[1]);
            DACDelay(hwp);
            hwp->writeDacData(hwp, cmap[2]);
            DACDelay(hwp);
        }
    }
    if (new_overscan && !hwp->ShowOverscan) {
        new_overscan = FALSE;
        for (i = 0; i < ndef; i++) {
            if (pdefs[i].pixel == overscan) {
                if ((pdefs[i].red != 0) ||
                    (pdefs[i].green != 0) || (pdefs[i].blue != 0)) {
                    new_overscan = TRUE;
                    tmp_overscan = overscan;
                    tmp = &(hwp->ModeReg.DAC[pdefs[i].pixel * 3]);
                }
                break;
            }
        }
        if (new_overscan) {
            /*
             * Find a black pixel, or the nearest match.
             */
            for (i = 255; i >= 0; i--) {
                cmap = &(hwp->ModeReg.DAC[i * 3]);
                if ((cmap[0] == 0) && (cmap[1] == 0) && (cmap[2] == 0)) {
                    overscan = i;
                    break;
                }
                else {
                    if ((cmap[0] < tmp[0]) &&
                        (cmap[1] < tmp[1]) && (cmap[2] < tmp[2])) {
                        tmp = cmap;
                        tmp_overscan = i;
                    }
                }
            }
            if (i < 0) {
                overscan = tmp_overscan;
            }
            hwp->ModeReg.Attribute[OVERSCAN] = overscan;
            if (writeColormap) {
                hwp->writeAttr(hwp, OVERSCAN, overscan);
            }
        }
    }

    if (writeColormap)
        hwp->disablePalette(hwp);
}

void
vgaInstallColormap(pmap)
ColormapPtr pmap;
{
    ColormapPtr oldmap = GetInstalledmiColormap(pmap->pScreen);
    int entries;
    Pixel *ppix;
    xrgb *prgb;
    xColorItem *defs;
    int i;

    if (pmap == oldmap)
        return;

    if ((pmap->pVisual->class | DynamicClass) == DirectColor)
        entries = (pmap->pVisual->redMask |
                   pmap->pVisual->greenMask | pmap->pVisual->blueMask) + 1;
    else
        entries = pmap->pVisual->ColormapEntries;

    ppix = (Pixel *) malloc(entries * sizeof(Pixel));
    prgb = (xrgb *) malloc(entries * sizeof(xrgb));
    defs = (xColorItem *) malloc(entries * sizeof(xColorItem));

    if (oldmap != NOMAPYET)
        WalkTree(pmap->pScreen, TellLostMap, &oldmap->mid);

    SetInstalledmiColormap(pmap->pScreen, pmap);

    for (i = 0; i < entries; i++)
        ppix[i] = i;

    QueryColors(pmap, entries, ppix, prgb, serverClient);

    for (i = 0; i < entries; i++) {     /* convert xrgbs to xColorItems */
        defs[i].pixel = ppix[i];
        defs[i].red = prgb[i].red;
        defs[i].green = prgb[i].green;
        defs[i].blue = prgb[i].blue;
        defs[i].flags = DoRed | DoGreen | DoBlue;
    }
    pmap->pScreen->StoreColors(pmap, entries, defs);

    WalkTree(pmap->pScreen, TellGainedMap, &pmap->mid);

    free(ppix);
    free(prgb);
    free(defs);
}

void
vgaUninstallColormap(pmap)
ColormapPtr pmap;
{

    ColormapPtr defColormap;

    if (pmap != GetInstalledmiColormap(pmap->pScreen))
        return;

    dixLookupResourceByType((void **) &defColormap,
                            pmap->pScreen->defColormap, RT_COLORMAP,
                            serverClient, DixInstallAccess);

    if (defColormap == GetInstalledmiColormap(pmap->pScreen))
        return;

    (*pmap->pScreen->InstallColormap) (defColormap);
}

void
vgaHandleColormaps(ScreenPtr pScreen, ScrnInfoPtr scrnp)
{
    if (scrnp->bitsPerPixel > 1) {
        if (scrnp->bitsPerPixel <= 8) { /* For 8bpp SVGA and VGA16 */
            pScreen->InstallColormap = vgaInstallColormap;
            pScreen->UninstallColormap = vgaUninstallColormap;
            pScreen->ListInstalledColormaps = vgaListInstalledColormaps;
            pScreen->StoreColors = vgaStoreColors;
        }
    }
}
