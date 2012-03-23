
/*
   Copyright (c) 1999 -  The XFree86 Project Inc.

   Written by Mark Vojkovich

*/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "windowstr.h"
#include "xf86str.h"
#include "mi.h"
#include "miline.h"
#include "xaa.h"
#include "xaalocal.h"
#include "xaawrap.h"
#include "xf86fbman.h"
#include "servermd.h"

void
XAAMoveOutOffscreenPixmaps(ScreenPtr pScreen)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    PixmapLinkPtr pLink = infoRec->OffscreenPixmaps;
    XAAPixmapPtr pPriv;

    while (pLink) {
        pPriv = XAA_GET_PIXMAP_PRIVATE(pLink->pPix);
        pLink->area = pPriv->offscreenArea;
        XAAMoveOutOffscreenPixmap(pLink->pPix);
        pLink = pLink->next;
    }
}

void
XAAMoveInOffscreenPixmaps(ScreenPtr pScreen)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    PixmapLinkPtr pLink = infoRec->OffscreenPixmaps;
    PixmapPtr pPix, pScreenPix, tmpPix;
    pointer data;
    XAAPixmapPtr pPriv;
    GCPtr pGC;
    FBAreaPtr area;

    pScreenPix = (*pScreen->GetScreenPixmap) (pScreen);

    while (pLink) {
        pPix = pLink->pPix;
        pPriv = XAA_GET_PIXMAP_PRIVATE(pPix);
        area = pLink->area;

        data = pPix->devPrivate.ptr;
        tmpPix = GetScratchPixmapHeader(pScreen,
                                        pPix->drawable.width,
                                        pPix->drawable.height,
                                        pPix->drawable.depth,
                                        pPix->drawable.bitsPerPixel,
                                        pPix->devKind, data);

        pPriv->freeData = FALSE;

        pPix->drawable.x = area->box.x1;
        pPix->drawable.y = area->box.y1;
        pPix->devKind = pScreenPix->devKind;
        pPix->devPrivate.ptr = pScreenPix->devPrivate.ptr;
        pPix->drawable.bitsPerPixel = infoRec->pScrn->bitsPerPixel;
        pPix->drawable.serialNumber = NEXT_SERIAL_NUMBER;

        if (!tmpPix) {
            pPriv->offscreenArea = area;
            free(data);
            pLink = pLink->next;
            continue;
        }

        pGC = GetScratchGC(pPix->drawable.depth, pScreen);
        ValidateGC((DrawablePtr) pPix, pGC);

        (*pGC->ops->CopyArea) ((DrawablePtr) tmpPix, (DrawablePtr) pPix, pGC,
                               0, 0, pPix->drawable.width,
                               pPix->drawable.height, 0, 0);

        free(data);
        tmpPix->devPrivate.ptr = NULL;

        FreeScratchGC(pGC);
        FreeScratchPixmapHeader(tmpPix);

        pPriv->offscreenArea = area;
        pLink->area = NULL;
        pLink = pLink->next;
    }
}

void
XAARemoveAreaCallback(FBAreaPtr area)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(area->pScreen);
    PixmapPtr pPix = (PixmapPtr) area->devPrivate.ptr;
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPix);

    XAAMoveOutOffscreenPixmap(pPix);

    pPriv->flags &= ~OFFSCREEN;

    DELIST_OFFSCREEN_PIXMAP(pPix);
}

void
XAAMoveOutOffscreenPixmap(PixmapPtr pPix)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPix);
    int width, height, devKind, bitsPerPixel;
    PixmapPtr tmpPix;
    unsigned char *data;
    GCPtr pGC;

    width = pPix->drawable.width;
    height = pPix->drawable.height;
    bitsPerPixel = pPix->drawable.bitsPerPixel;

    devKind = BitmapBytePad(width * bitsPerPixel);
    if (!(data = malloc(devKind * height)))
        FatalError("Out of memory\n");

    tmpPix = GetScratchPixmapHeader(pScreen, width, height,
                                    pPix->drawable.depth, bitsPerPixel, devKind,
                                    data);
    if (!tmpPix) {
        free(data);
        FatalError("Out of memory\n");
    }

    pGC = GetScratchGC(pPix->drawable.depth, pScreen);
    ValidateGC((DrawablePtr) tmpPix, pGC);

    (*pGC->ops->CopyArea) ((DrawablePtr) pPix, (DrawablePtr) tmpPix,
                           pGC, 0, 0, width, height, 0, 0);

    FreeScratchGC(pGC);
    FreeScratchPixmapHeader(tmpPix);

    pPix->drawable.x = 0;
    pPix->drawable.y = 0;
    pPix->devKind = devKind;
    pPix->devPrivate.ptr = data;
    pPix->drawable.serialNumber = NEXT_SERIAL_NUMBER;

    pPriv->offscreenArea = NULL;
    pPriv->freeData = TRUE;
}
