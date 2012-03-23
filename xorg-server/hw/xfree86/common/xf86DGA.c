/*
 * Copyright (c) 1998-2002 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 *
 * Written by Mark Vojkovich
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "xf86str.h"
#include "xf86Priv.h"
#include "dgaproc.h"
#include <X11/extensions/xf86dgaproto.h>
#include "colormapst.h"
#include "pixmapstr.h"
#include "inputstr.h"
#include "globals.h"
#include "servermd.h"
#include "micmap.h"
#include "xkbsrv.h"
#include "xf86Xinput.h"
#include "exglobals.h"
#include "exevents.h"
#include "eventstr.h"
#include "eventconvert.h"

#include "mi.h"

static DevPrivateKeyRec DGAScreenKeyRec;

#define DGAScreenKeyRegistered dixPrivateKeyRegistered(&DGAScreenKeyRec)
static Bool mieq_installed;

static Bool DGACloseScreen(int i, ScreenPtr pScreen);
static void DGADestroyColormap(ColormapPtr pmap);
static void DGAInstallColormap(ColormapPtr pmap);
static void DGAUninstallColormap(ColormapPtr pmap);
static void DGAHandleEvent(int screen_num, InternalEvent *event,
                           DeviceIntPtr device);

static void
 DGACopyModeInfo(DGAModePtr mode, XDGAModePtr xmode);

int *XDGAEventBase = NULL;

#define DGA_GET_SCREEN_PRIV(pScreen) ((DGAScreenPtr) \
    dixLookupPrivate(&(pScreen)->devPrivates, &DGAScreenKeyRec))

typedef struct _FakedVisualList {
    Bool free;
    VisualPtr pVisual;
    struct _FakedVisualList *next;
} FakedVisualList;

typedef struct {
    ScrnInfoPtr pScrn;
    int numModes;
    DGAModePtr modes;
    CloseScreenProcPtr CloseScreen;
    DestroyColormapProcPtr DestroyColormap;
    InstallColormapProcPtr InstallColormap;
    UninstallColormapProcPtr UninstallColormap;
    DGADevicePtr current;
    DGAFunctionPtr funcs;
    int input;
    ClientPtr client;
    int pixmapMode;
    FakedVisualList *fakedVisuals;
    ColormapPtr dgaColormap;
    ColormapPtr savedColormap;
    Bool grabMouse;
    Bool grabKeyboard;
} DGAScreenRec, *DGAScreenPtr;

Bool
DGAInit(ScreenPtr pScreen, DGAFunctionPtr funcs, DGAModePtr modes, int num)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    DGAScreenPtr pScreenPriv;
    int i;

    if (!funcs || !funcs->SetMode || !funcs->OpenFramebuffer)
        return FALSE;

    if (!modes || num <= 0)
        return FALSE;

    if (!dixRegisterPrivateKey(&DGAScreenKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;

    pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    if (!pScreenPriv) {
        if (!(pScreenPriv = (DGAScreenPtr) malloc(sizeof(DGAScreenRec))))
            return FALSE;
        dixSetPrivate(&pScreen->devPrivates, &DGAScreenKeyRec, pScreenPriv);
        pScreenPriv->CloseScreen = pScreen->CloseScreen;
        pScreen->CloseScreen = DGACloseScreen;
        pScreenPriv->DestroyColormap = pScreen->DestroyColormap;
        pScreen->DestroyColormap = DGADestroyColormap;
        pScreenPriv->InstallColormap = pScreen->InstallColormap;
        pScreen->InstallColormap = DGAInstallColormap;
        pScreenPriv->UninstallColormap = pScreen->UninstallColormap;
        pScreen->UninstallColormap = DGAUninstallColormap;
    }

    pScreenPriv->pScrn = pScrn;
    pScreenPriv->numModes = num;
    pScreenPriv->modes = modes;
    pScreenPriv->current = NULL;

    pScreenPriv->funcs = funcs;
    pScreenPriv->input = 0;
    pScreenPriv->client = NULL;
    pScreenPriv->fakedVisuals = NULL;
    pScreenPriv->dgaColormap = NULL;
    pScreenPriv->savedColormap = NULL;
    pScreenPriv->grabMouse = FALSE;
    pScreenPriv->grabKeyboard = FALSE;

    for (i = 0; i < num; i++)
        modes[i].num = i + 1;

#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        for (i = 0; i < num; i++)
            modes[i].flags &= ~DGA_PIXMAP_AVAILABLE;
#endif

    return TRUE;
}

/* DGAReInitModes allows the driver to re-initialize
 * the DGA mode list.
 */

Bool
DGAReInitModes(ScreenPtr pScreen, DGAModePtr modes, int num)
{
    DGAScreenPtr pScreenPriv;
    int i;

    /* No DGA? Ignore call (but don't make it look like it failed) */
    if (!DGAScreenKeyRegistered)
        return TRUE;

    pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    /* Same as above */
    if (!pScreenPriv)
        return TRUE;

    /* Can't do this while DGA is active */
    if (pScreenPriv->current)
        return FALSE;

    /* Quick sanity check */
    if (!num)
        modes = NULL;
    else if (!modes)
        num = 0;

    pScreenPriv->numModes = num;
    pScreenPriv->modes = modes;

    /* This practically disables DGA. So be it. */
    if (!num)
        return TRUE;

    for (i = 0; i < num; i++)
        modes[i].num = i + 1;

#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        for (i = 0; i < num; i++)
            modes[i].flags &= ~DGA_PIXMAP_AVAILABLE;
#endif

    return TRUE;
}

static void
FreeMarkedVisuals(ScreenPtr pScreen)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);
    FakedVisualList *prev, *curr, *tmp;

    if (!pScreenPriv->fakedVisuals)
        return;

    prev = NULL;
    curr = pScreenPriv->fakedVisuals;

    while (curr) {
        if (curr->free) {
            tmp = curr;
            curr = curr->next;
            if (prev)
                prev->next = curr;
            else
                pScreenPriv->fakedVisuals = curr;
            free(tmp->pVisual);
            free(tmp);
        }
        else {
            prev = curr;
            curr = curr->next;
        }
    }
}

static Bool
DGACloseScreen(int i, ScreenPtr pScreen)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    if (mieq_installed) {
        mieqSetHandler(ET_DGAEvent, NULL);
        mieq_installed = FALSE;
    }

    FreeMarkedVisuals(pScreen);

    pScreen->CloseScreen = pScreenPriv->CloseScreen;
    pScreen->DestroyColormap = pScreenPriv->DestroyColormap;
    pScreen->InstallColormap = pScreenPriv->InstallColormap;
    pScreen->UninstallColormap = pScreenPriv->UninstallColormap;

    /* DGAShutdown() should have ensured that no DGA
       screen were active by here */

    free(pScreenPriv);

    return ((*pScreen->CloseScreen) (i, pScreen));
}

static void
DGADestroyColormap(ColormapPtr pmap)
{
    ScreenPtr pScreen = pmap->pScreen;
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);
    VisualPtr pVisual = pmap->pVisual;

    if (pScreenPriv->fakedVisuals) {
        FakedVisualList *curr = pScreenPriv->fakedVisuals;

        while (curr) {
            if (curr->pVisual == pVisual) {
                /* We can't get rid of them yet since FreeColormap
                   still needs the pVisual during the cleanup */
                curr->free = TRUE;
                break;
            }
            curr = curr->next;
        }
    }

    if (pScreenPriv->DestroyColormap) {
        pScreen->DestroyColormap = pScreenPriv->DestroyColormap;
        (*pScreen->DestroyColormap) (pmap);
        pScreen->DestroyColormap = DGADestroyColormap;
    }
}

static void
DGAInstallColormap(ColormapPtr pmap)
{
    ScreenPtr pScreen = pmap->pScreen;
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    if (pScreenPriv->current && pScreenPriv->dgaColormap) {
        if (pmap != pScreenPriv->dgaColormap) {
            pScreenPriv->savedColormap = pmap;
            pmap = pScreenPriv->dgaColormap;
        }
    }

    pScreen->InstallColormap = pScreenPriv->InstallColormap;
    (*pScreen->InstallColormap) (pmap);
    pScreen->InstallColormap = DGAInstallColormap;
}

static void
DGAUninstallColormap(ColormapPtr pmap)
{
    ScreenPtr pScreen = pmap->pScreen;
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    if (pScreenPriv->current && pScreenPriv->dgaColormap) {
        if (pmap == pScreenPriv->dgaColormap) {
            pScreenPriv->dgaColormap = NULL;
        }
    }

    pScreen->UninstallColormap = pScreenPriv->UninstallColormap;
    (*pScreen->UninstallColormap) (pmap);
    pScreen->UninstallColormap = DGAUninstallColormap;
}

int
xf86SetDGAMode(int index, int num, DGADevicePtr devRet)
{
    ScreenPtr pScreen = screenInfo.screens[index];
    DGAScreenPtr pScreenPriv;
    ScrnInfoPtr pScrn;
    DGADevicePtr device;
    PixmapPtr pPix = NULL;
    DGAModePtr pMode = NULL;

    /* First check if DGAInit was successful on this screen */
    if (!DGAScreenKeyRegistered)
        return BadValue;
    pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);
    if (!pScreenPriv)
        return BadValue;
    pScrn = pScreenPriv->pScrn;

    if (!num) {
        if (pScreenPriv->current) {
            PixmapPtr oldPix = pScreenPriv->current->pPix;

            if (oldPix) {
                if (oldPix->drawable.id)
                    FreeResource(oldPix->drawable.id, RT_NONE);
                else
                    (*pScreen->DestroyPixmap) (oldPix);
            }
            free(pScreenPriv->current);
            pScreenPriv->current = NULL;
            pScrn->vtSema = TRUE;
            (*pScreenPriv->funcs->SetMode) (pScrn, NULL);
            if (pScreenPriv->savedColormap) {
                (*pScreen->InstallColormap) (pScreenPriv->savedColormap);
                pScreenPriv->savedColormap = NULL;
            }
            pScreenPriv->dgaColormap = NULL;
            (*pScrn->EnableDisableFBAccess) (index, TRUE);

            FreeMarkedVisuals(pScreen);
        }

        pScreenPriv->grabMouse = FALSE;
        pScreenPriv->grabKeyboard = FALSE;

        return Success;
    }

    if (!pScrn->vtSema && !pScreenPriv->current)        /* Really switched away */
        return BadAlloc;

    if ((num > 0) && (num <= pScreenPriv->numModes))
        pMode = &(pScreenPriv->modes[num - 1]);
    else
        return BadValue;

    if (!(device = (DGADevicePtr) malloc(sizeof(DGADeviceRec))))
        return BadAlloc;

    if (!pScreenPriv->current) {
        Bool oldVTSema = pScrn->vtSema;

        pScrn->vtSema = FALSE;  /* kludge until we rewrite VT switching */
        (*pScrn->EnableDisableFBAccess) (index, FALSE);
        pScrn->vtSema = oldVTSema;
    }

    if (!(*pScreenPriv->funcs->SetMode) (pScrn, pMode)) {
        free(device);
        return BadAlloc;
    }

    pScrn->currentMode = pMode->mode;

    if (!pScreenPriv->current && !pScreenPriv->input) {
        /* if it's multihead we need to warp the cursor off of
           our screen so it doesn't get trapped  */
    }

    pScrn->vtSema = FALSE;

    if (pScreenPriv->current) {
        PixmapPtr oldPix = pScreenPriv->current->pPix;

        if (oldPix) {
            if (oldPix->drawable.id)
                FreeResource(oldPix->drawable.id, RT_NONE);
            else
                (*pScreen->DestroyPixmap) (oldPix);
        }
        free(pScreenPriv->current);
        pScreenPriv->current = NULL;
    }

    if (pMode->flags & DGA_PIXMAP_AVAILABLE) {
        if ((pPix = (*pScreen->CreatePixmap) (pScreen, 0, 0, pMode->depth, 0))) {
            (*pScreen->ModifyPixmapHeader) (pPix,
                                            pMode->pixmapWidth,
                                            pMode->pixmapHeight, pMode->depth,
                                            pMode->bitsPerPixel,
                                            pMode->bytesPerScanline,
                                            (pointer) (pMode->address));
        }
    }

    devRet->mode = device->mode = pMode;
    devRet->pPix = device->pPix = pPix;
    pScreenPriv->current = device;
    pScreenPriv->pixmapMode = FALSE;
    pScreenPriv->grabMouse = TRUE;
    pScreenPriv->grabKeyboard = TRUE;

    if (!mieq_installed) {
        mieqSetHandler(ET_DGAEvent, DGAHandleEvent);
        mieq_installed = TRUE;
    }

    return Success;
}

/*********** exported ones ***************/

void
DGASetInputMode(int index, Bool keyboard, Bool mouse)
{
    ScreenPtr pScreen = screenInfo.screens[index];
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    if (pScreenPriv) {
        pScreenPriv->grabMouse = mouse;
        pScreenPriv->grabKeyboard = keyboard;

        if (!mieq_installed) {
            mieqSetHandler(ET_DGAEvent, DGAHandleEvent);
            mieq_installed = TRUE;
        }
    }
}

Bool
DGAChangePixmapMode(int index, int *x, int *y, int mode)
{
    DGAScreenPtr pScreenPriv;
    DGADevicePtr pDev;
    DGAModePtr pMode;
    PixmapPtr pPix;

    if (!DGAScreenKeyRegistered)
        return FALSE;

    pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    if (!pScreenPriv || !pScreenPriv->current || !pScreenPriv->current->pPix)
        return FALSE;

    pDev = pScreenPriv->current;
    pPix = pDev->pPix;
    pMode = pDev->mode;

    if (mode) {
        int shift = 2;

        if (*x > (pMode->pixmapWidth - pMode->viewportWidth))
            *x = pMode->pixmapWidth - pMode->viewportWidth;
        if (*y > (pMode->pixmapHeight - pMode->viewportHeight))
            *y = pMode->pixmapHeight - pMode->viewportHeight;

        switch (xf86Screens[index]->bitsPerPixel) {
        case 16:
            shift = 1;
            break;
        case 32:
            shift = 0;
            break;
        default:
            break;
        }

        if (BITMAP_SCANLINE_PAD == 64)
            shift++;

        *x = (*x >> shift) << shift;

        pPix->drawable.x = *x;
        pPix->drawable.y = *y;
        pPix->drawable.width = pMode->viewportWidth;
        pPix->drawable.height = pMode->viewportHeight;
    }
    else {
        pPix->drawable.x = 0;
        pPix->drawable.y = 0;
        pPix->drawable.width = pMode->pixmapWidth;
        pPix->drawable.height = pMode->pixmapHeight;
    }
    pPix->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pScreenPriv->pixmapMode = mode;

    return TRUE;
}

Bool
DGAAvailable(int index)
{
    if (!DGAScreenKeyRegistered)
        return FALSE;

    if (DGA_GET_SCREEN_PRIV(screenInfo.screens[index]))
        return TRUE;

    return FALSE;
}

Bool
DGAActive(int index)
{
    DGAScreenPtr pScreenPriv;

    if (!DGAScreenKeyRegistered)
        return FALSE;

    pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    if (pScreenPriv && pScreenPriv->current)
        return TRUE;

    return FALSE;
}

/* Called by the event code in case the server is abruptly terminated */

void
DGAShutdown(void)
{
    ScrnInfoPtr pScrn;
    int i;

    if (!DGAScreenKeyRegistered)
        return;

    for (i = 0; i < screenInfo.numScreens; i++) {
        pScrn = xf86Screens[i];

        (void) (*pScrn->SetDGAMode) (pScrn->scrnIndex, 0, NULL);
    }
}

/* Called by the extension to initialize a mode */

int
DGASetMode(int index, int num, XDGAModePtr mode, PixmapPtr *pPix)
{
    ScrnInfoPtr pScrn = xf86Screens[index];
    DGADeviceRec device;
    int ret;

    /* We rely on the extension to check that DGA is available */

    ret = (*pScrn->SetDGAMode) (index, num, &device);
    if ((ret == Success) && num) {
        DGACopyModeInfo(device.mode, mode);
        *pPix = device.pPix;
    }

    return ret;
}

/* Called from the extension to let the DDX know which events are requested */

void
DGASelectInput(int index, ClientPtr client, long mask)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is available */
    pScreenPriv->client = client;
    pScreenPriv->input = mask;
}

int
DGAGetViewportStatus(int index)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is active */

    if (!pScreenPriv->funcs->GetViewport)
        return 0;

    return (*pScreenPriv->funcs->GetViewport) (pScreenPriv->pScrn);
}

int
DGASetViewport(int index, int x, int y, int mode)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    if (pScreenPriv->funcs->SetViewport)
        (*pScreenPriv->funcs->SetViewport) (pScreenPriv->pScrn, x, y, mode);
    return Success;
}

static int
BitsClear(CARD32 data)
{
    int bits = 0;
    CARD32 mask;

    for (mask = 1; mask; mask <<= 1) {
        if (!(data & mask))
            bits++;
        else
            break;
    }

    return bits;
}

int
DGACreateColormap(int index, ClientPtr client, int id, int mode, int alloc)
{
    ScreenPtr pScreen = screenInfo.screens[index];
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);
    FakedVisualList *fvlp;
    VisualPtr pVisual;
    DGAModePtr pMode;
    ColormapPtr pmap;

    if (!mode || (mode > pScreenPriv->numModes))
        return BadValue;

    if ((alloc != AllocNone) && (alloc != AllocAll))
        return BadValue;

    pMode = &(pScreenPriv->modes[mode - 1]);

    if (!(pVisual = malloc(sizeof(VisualRec))))
        return BadAlloc;

    pVisual->vid = FakeClientID(0);
    pVisual->class = pMode->visualClass;
    pVisual->nplanes = pMode->depth;
    pVisual->ColormapEntries = 1 << pMode->depth;
    pVisual->bitsPerRGBValue = (pMode->depth + 2) / 3;

    switch (pVisual->class) {
    case PseudoColor:
    case GrayScale:
    case StaticGray:
        pVisual->bitsPerRGBValue = 8;   /* not quite */
        pVisual->redMask = 0;
        pVisual->greenMask = 0;
        pVisual->blueMask = 0;
        pVisual->offsetRed = 0;
        pVisual->offsetGreen = 0;
        pVisual->offsetBlue = 0;
        break;
    case DirectColor:
    case TrueColor:
        pVisual->ColormapEntries = 1 << pVisual->bitsPerRGBValue;
        /* fall through */
    case StaticColor:
        pVisual->redMask = pMode->red_mask;
        pVisual->greenMask = pMode->green_mask;
        pVisual->blueMask = pMode->blue_mask;
        pVisual->offsetRed = BitsClear(pVisual->redMask);
        pVisual->offsetGreen = BitsClear(pVisual->greenMask);
        pVisual->offsetBlue = BitsClear(pVisual->blueMask);
    }

    if (!(fvlp = malloc(sizeof(FakedVisualList)))) {
        free(pVisual);
        return BadAlloc;
    }

    fvlp->free = FALSE;
    fvlp->pVisual = pVisual;
    fvlp->next = pScreenPriv->fakedVisuals;
    pScreenPriv->fakedVisuals = fvlp;

    LEGAL_NEW_RESOURCE(id, client);

    return CreateColormap(id, pScreen, pVisual, &pmap, alloc, client->index);
}

/*  Called by the extension to install a colormap on DGA active screens */

void
DGAInstallCmap(ColormapPtr cmap)
{
    ScreenPtr pScreen = cmap->pScreen;
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    /* We rely on the extension to check that DGA is active */

    if (!pScreenPriv->dgaColormap)
        pScreenPriv->savedColormap = GetInstalledmiColormap(pScreen);

    pScreenPriv->dgaColormap = cmap;

    (*pScreen->InstallColormap) (cmap);
}

int
DGASync(int index)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is active */

    if (pScreenPriv->funcs->Sync)
        (*pScreenPriv->funcs->Sync) (pScreenPriv->pScrn);

    return Success;
}

int
DGAFillRect(int index, int x, int y, int w, int h, unsigned long color)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is active */

    if (pScreenPriv->funcs->FillRect &&
        (pScreenPriv->current->mode->flags & DGA_FILL_RECT)) {

        (*pScreenPriv->funcs->FillRect) (pScreenPriv->pScrn, x, y, w, h, color);
        return Success;
    }
    return BadMatch;
}

int
DGABlitRect(int index, int srcx, int srcy, int w, int h, int dstx, int dsty)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is active */

    if (pScreenPriv->funcs->BlitRect &&
        (pScreenPriv->current->mode->flags & DGA_BLIT_RECT)) {

        (*pScreenPriv->funcs->BlitRect) (pScreenPriv->pScrn,
                                         srcx, srcy, w, h, dstx, dsty);
        return Success;
    }
    return BadMatch;
}

int
DGABlitTransRect(int index,
                 int srcx, int srcy,
                 int w, int h, int dstx, int dsty, unsigned long color)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is active */

    if (pScreenPriv->funcs->BlitTransRect &&
        (pScreenPriv->current->mode->flags & DGA_BLIT_RECT_TRANS)) {

        (*pScreenPriv->funcs->BlitTransRect) (pScreenPriv->pScrn,
                                              srcx, srcy, w, h, dstx, dsty,
                                              color);
        return Success;
    }
    return BadMatch;
}

int
DGAGetModes(int index)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is available */

    return pScreenPriv->numModes;
}

int
DGAGetModeInfo(int index, XDGAModePtr mode, int num)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is available */

    if ((num <= 0) || (num > pScreenPriv->numModes))
        return BadValue;

    DGACopyModeInfo(&(pScreenPriv->modes[num - 1]), mode);

    return Success;
}

static void
DGACopyModeInfo(DGAModePtr mode, XDGAModePtr xmode)
{
    DisplayModePtr dmode = mode->mode;

    xmode->num = mode->num;
    xmode->name = dmode->name;
    xmode->VSync_num = (int) (dmode->VRefresh * 1000.0);
    xmode->VSync_den = 1000;
    xmode->flags = mode->flags;
    xmode->imageWidth = mode->imageWidth;
    xmode->imageHeight = mode->imageHeight;
    xmode->pixmapWidth = mode->pixmapWidth;
    xmode->pixmapHeight = mode->pixmapHeight;
    xmode->bytesPerScanline = mode->bytesPerScanline;
    xmode->byteOrder = mode->byteOrder;
    xmode->depth = mode->depth;
    xmode->bitsPerPixel = mode->bitsPerPixel;
    xmode->red_mask = mode->red_mask;
    xmode->green_mask = mode->green_mask;
    xmode->blue_mask = mode->blue_mask;
    xmode->visualClass = mode->visualClass;
    xmode->viewportWidth = mode->viewportWidth;
    xmode->viewportHeight = mode->viewportHeight;
    xmode->xViewportStep = mode->xViewportStep;
    xmode->yViewportStep = mode->yViewportStep;
    xmode->maxViewportX = mode->maxViewportX;
    xmode->maxViewportY = mode->maxViewportY;
    xmode->viewportFlags = mode->viewportFlags;
    xmode->reserved1 = mode->reserved1;
    xmode->reserved2 = mode->reserved2;
    xmode->offset = mode->offset;

    if (dmode->Flags & V_INTERLACE)
        xmode->flags |= DGA_INTERLACED;
    if (dmode->Flags & V_DBLSCAN)
        xmode->flags |= DGA_DOUBLESCAN;
}

Bool
DGAVTSwitch(void)
{
    ScreenPtr pScreen;
    int i;

    for (i = 0; i < screenInfo.numScreens; i++) {
        pScreen = screenInfo.screens[i];

        /* Alternatively, this could send events to DGA clients */

        if (DGAScreenKeyRegistered) {
            DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

            if (pScreenPriv && pScreenPriv->current)
                return FALSE;
        }
    }

    return TRUE;
}

Bool
DGAStealKeyEvent(DeviceIntPtr dev, int index, int key_code, int is_down)
{
    DGAScreenPtr pScreenPriv;
    DGAEvent event;

    if (!DGAScreenKeyRegistered)        /* no DGA */
        return FALSE;

    if (key_code < 8 || key_code > 255)
        return FALSE;

    pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    if (!pScreenPriv || !pScreenPriv->grabKeyboard)     /* no direct mode */
        return FALSE;

    memset(&event, 0, sizeof(event));
    event.header = ET_Internal;
    event.type = ET_DGAEvent;
    event.length = sizeof(event);
    event.time = GetTimeInMillis();
    event.subtype = (is_down ? ET_KeyPress : ET_KeyRelease);
    event.detail = key_code;
    event.dx = 0;
    event.dy = 0;
    mieqEnqueue(dev, (InternalEvent *) &event);

    return TRUE;
}

Bool
DGAStealMotionEvent(DeviceIntPtr dev, int index, int dx, int dy)
{
    DGAScreenPtr pScreenPriv;
    DGAEvent event;

    if (!DGAScreenKeyRegistered)        /* no DGA */
        return FALSE;

    pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    if (!pScreenPriv || !pScreenPriv->grabMouse)        /* no direct mode */
        return FALSE;

    memset(&event, 0, sizeof(event));
    event.header = ET_Internal;
    event.type = ET_DGAEvent;
    event.length = sizeof(event);
    event.time = GetTimeInMillis();
    event.subtype = ET_Motion;
    event.detail = 0;
    event.dx = dx;
    event.dy = dy;
    mieqEnqueue(dev, (InternalEvent *) &event);
    return TRUE;
}

Bool
DGAStealButtonEvent(DeviceIntPtr dev, int index, int button, int is_down)
{
    DGAScreenPtr pScreenPriv;
    DGAEvent event;

    if (!DGAScreenKeyRegistered)        /* no DGA */
        return FALSE;

    pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    if (!pScreenPriv || !pScreenPriv->grabMouse)
        return FALSE;

    memset(&event, 0, sizeof(event));
    event.header = ET_Internal;
    event.type = ET_DGAEvent;
    event.length = sizeof(event);
    event.time = GetTimeInMillis();
    event.subtype = (is_down ? ET_ButtonPress : ET_ButtonRelease);
    event.detail = button;
    event.dx = 0;
    event.dy = 0;
    mieqEnqueue(dev, (InternalEvent *) &event);

    return TRUE;
}

/* We have the power to steal or modify events that are about to get queued */

#define NoSuchEvent 0x80000000  /* so doesn't match NoEventMask */
static Mask filters[] = {
    NoSuchEvent,                /* 0 */
    NoSuchEvent,                /* 1 */
    KeyPressMask,               /* KeyPress */
    KeyReleaseMask,             /* KeyRelease */
    ButtonPressMask,            /* ButtonPress */
    ButtonReleaseMask,          /* ButtonRelease */
    PointerMotionMask,          /* MotionNotify (initial state) */
};

static void
DGAProcessKeyboardEvent(ScreenPtr pScreen, DGAEvent * event, DeviceIntPtr keybd)
{
    KeyClassPtr keyc = keybd->key;
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);
    DeviceIntPtr pointer = GetMaster(keybd, POINTER_OR_FLOAT);
    DeviceEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.header = ET_Internal;
    ev.length = sizeof(ev);
    ev.detail.key = event->detail;
    ev.type = event->subtype;
    ev.root_x = 0;
    ev.root_y = 0;
    ev.corestate = XkbStateFieldFromRec(&keyc->xkbInfo->state);
    ev.corestate |= pointer->button->state;

    UpdateDeviceState(keybd, &ev);

    /*
     * Deliver the DGA event
     */
    if (pScreenPriv->client) {
        dgaEvent de;

        de.u.u.type = *XDGAEventBase + GetCoreType(ev.type);
        de.u.u.detail = event->detail;
        de.u.event.time = event->time;
        de.u.event.dx = event->dx;
        de.u.event.dy = event->dy;
        de.u.event.screen = pScreen->myNum;
        de.u.event.state = ev.corestate;

        /* If the DGA client has selected input, then deliver based on the usual filter */
        TryClientEvents(pScreenPriv->client, keybd, (xEvent *) &de, 1,
                        filters[ev.type], pScreenPriv->input, 0);
    }
    else {
        /* If the keyboard is actively grabbed, deliver a grabbed core event */
        if (keybd->deviceGrab.grab && !keybd->deviceGrab.fromPassiveGrab) {
            ev.detail.key = event->detail;
            ev.time = event->time;
            ev.root_x = event->dx;
            ev.root_y = event->dy;
            ev.corestate = event->state;
            ev.deviceid = keybd->id;
            DeliverGrabbedEvent((InternalEvent *) &ev, keybd, FALSE);
        }
    }
}

static void
DGAProcessPointerEvent(ScreenPtr pScreen, DGAEvent * event, DeviceIntPtr mouse)
{
    ButtonClassPtr butc = mouse->button;
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);
    DeviceEvent ev;
    DeviceIntPtr master = GetMaster(mouse, MASTER_KEYBOARD);

    memset(&ev, 0, sizeof(ev));
    ev.header = ET_Internal;
    ev.length = sizeof(ev);
    ev.type = event->subtype;
    ev.corestate = butc ? butc->state : 0;
    if (master && master->key)
        ev.corestate |= XkbStateFieldFromRec(&master->key->xkbInfo->state);

    UpdateDeviceState(mouse, &ev);

    /*
     * Deliver the DGA event
     */
    if (pScreenPriv->client) {
        dgaEvent de;
        int coreEquiv;

        coreEquiv = GetCoreType(ev.type);

        de.u.u.type = *XDGAEventBase + coreEquiv;
        de.u.u.detail = event->detail;
        de.u.event.time = event->time;
        de.u.event.dx = event->dx;
        de.u.event.dy = event->dy;
        de.u.event.screen = pScreen->myNum;
        de.u.event.state = ev.corestate;

        /* If the DGA client has selected input, then deliver based on the usual filter */
        TryClientEvents(pScreenPriv->client, mouse, (xEvent *) &de, 1,
                        filters[coreEquiv], pScreenPriv->input, 0);
    }
    else {
        /* If the pointer is actively grabbed, deliver a grabbed core event */
        if (mouse->deviceGrab.grab && !mouse->deviceGrab.fromPassiveGrab) {
            ev.detail.button = event->detail;
            ev.time = event->time;
            ev.root_x = event->dx;
            ev.root_y = event->dy;
            ev.corestate = event->state;
            /* DGA is core only, so valuators.data doesn't actually matter.
             * Mask must be set for EventToCore to create motion events. */
            SetBit(ev.valuators.mask, 0);
            SetBit(ev.valuators.mask, 1);
            DeliverGrabbedEvent((InternalEvent *) &ev, mouse, FALSE);
        }
    }
}

Bool
DGAOpenFramebuffer(int index,
                   char **name,
                   unsigned char **mem, int *size, int *offset, int *flags)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is available */

    return (*pScreenPriv->funcs->OpenFramebuffer) (pScreenPriv->pScrn,
                                                   name, mem, size, offset,
                                                   flags);
}

void
DGACloseFramebuffer(int index)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);

    /* We rely on the extension to check that DGA is available */
    if (pScreenPriv->funcs->CloseFramebuffer)
        (*pScreenPriv->funcs->CloseFramebuffer) (pScreenPriv->pScrn);
}

/*  For DGA 1.0 backwards compatibility only */

int
DGAGetOldDGAMode(int index)
{
    DGAScreenPtr pScreenPriv = DGA_GET_SCREEN_PRIV(screenInfo.screens[index]);
    ScrnInfoPtr pScrn = pScreenPriv->pScrn;
    DGAModePtr mode;
    int i, w, h, p;

    /* We rely on the extension to check that DGA is available */

    w = pScrn->currentMode->HDisplay;
    h = pScrn->currentMode->VDisplay;
    p = pad_to_int32(pScrn->displayWidth * bits_to_bytes(pScrn->bitsPerPixel));

    for (i = 0; i < pScreenPriv->numModes; i++) {
        mode = &(pScreenPriv->modes[i]);

        if ((mode->viewportWidth == w) && (mode->viewportHeight == h) &&
            (mode->bytesPerScanline == p) &&
            (mode->bitsPerPixel == pScrn->bitsPerPixel) &&
            (mode->depth == pScrn->depth)) {

            return mode->num;
        }
    }

    return 0;
}

static void
DGAHandleEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
    DGAEvent *event = &ev->dga_event;
    ScreenPtr pScreen = screenInfo.screens[screen_num];
    DGAScreenPtr pScreenPriv;

    /* no DGA */
    if (!DGAScreenKeyRegistered || XDGAEventBase == 0)
        return;
    pScreenPriv = DGA_GET_SCREEN_PRIV(pScreen);

    /* DGA not initialized on this screen */
    if (!pScreenPriv)
        return;

    if (!IsMaster(device))
        return;

    switch (event->subtype) {
    case KeyPress:
    case KeyRelease:
        DGAProcessKeyboardEvent(pScreen, event, device);
        break;
    case MotionNotify:
    case ButtonPress:
    case ButtonRelease:
        DGAProcessPointerEvent(pScreen, event, device);
        break;
    default:
        break;
    }
}
