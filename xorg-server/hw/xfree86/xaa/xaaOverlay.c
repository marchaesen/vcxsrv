
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "windowstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "xaawrap.h"
#include "gcstruct.h"
#include "pixmapstr.h"
#include "mioverlay.h"

#ifdef PANORAMIX
#include "panoramiX.h"
#include "panoramiXsrv.h"
#endif

static void
XAACopyWindow8_32(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    DDXPointPtr pptSrc, ppt;
    RegionRec rgnDst;
    BoxPtr pbox;
    int dx, dy, nbox;
    WindowPtr pwinRoot;
    ScreenPtr pScreen = pWin->drawable.pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_DRAWABLE((&pWin->drawable));
    Bool doUnderlay = miOverlayCopyUnderlay(pScreen);
    RegionPtr borderClip = &pWin->borderClip;
    Bool freeReg = FALSE;

    if (!infoRec->pScrn->vtSema || !infoRec->ScreenToScreenBitBlt ||
        (infoRec->ScreenToScreenBitBltFlags & NO_PLANEMASK)) {
        XAA_SCREEN_PROLOGUE(pScreen, CopyWindow);
        if (infoRec->pScrn->vtSema && infoRec->NeedToSync) {
            (*infoRec->Sync) (infoRec->pScrn);
            infoRec->NeedToSync = FALSE;
        }
        (*pScreen->CopyWindow) (pWin, ptOldOrg, prgnSrc);
        XAA_SCREEN_EPILOGUE(pScreen, CopyWindow, XAACopyWindow8_32);
        return;
    }

    pwinRoot = pScreen->root;

    if (doUnderlay)
        freeReg = miOverlayCollectUnderlayRegions(pWin, &borderClip);

    RegionNull(&rgnDst);

    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;
    RegionTranslate(prgnSrc, -dx, -dy);
    RegionIntersect(&rgnDst, borderClip, prgnSrc);

    pbox = RegionRects(&rgnDst);
    nbox = RegionNumRects(&rgnDst);
    if (!nbox || !(pptSrc = (DDXPointPtr) malloc(nbox * sizeof(DDXPointRec)))) {
        RegionUninit(&rgnDst);
        return;
    }
    ppt = pptSrc;

    while (nbox--) {
        ppt->x = pbox->x1 + dx;
        ppt->y = pbox->y1 + dy;
        ppt++;
        pbox++;
    }

    infoRec->ScratchGC.planemask = doUnderlay ? 0x00ffffff : 0xff000000;
    infoRec->ScratchGC.alu = GXcopy;

    XAADoBitBlt((DrawablePtr) pwinRoot, (DrawablePtr) pwinRoot,
                &(infoRec->ScratchGC), &rgnDst, pptSrc);

    free(pptSrc);
    RegionUninit(&rgnDst);
    if (freeReg)
        RegionDestroy(borderClip);
}

static void
XAASetColorKey8_32(ScreenPtr pScreen, int nbox, BoxPtr pbox)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    ScrnInfoPtr pScrn = infoRec->pScrn;

    /* I'm counting on writes being clipped away while switched away.
       If this isn't going to be true then I need to be wrapping instead. */
    if (!infoRec->pScrn->vtSema)
        return;

    (*infoRec->FillSolidRects) (pScrn, pScrn->colorKey << 24, GXcopy,
                                0xff000000, nbox, pbox);

    SET_SYNC_FLAG(infoRec);
}

void
XAASetupOverlay8_32Planar(ScreenPtr pScreen)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    int i;

    pScreen->CopyWindow = XAACopyWindow8_32;

    if (!(infoRec->FillSolidRectsFlags & NO_PLANEMASK))
        miOverlaySetTransFunction(pScreen, XAASetColorKey8_32);

    infoRec->FullPlanemask = ~0;
    for (i = 0; i < 32; i++)    /* haven't thought about this much */
        infoRec->FullPlanemasks[i] = ~0;
}
