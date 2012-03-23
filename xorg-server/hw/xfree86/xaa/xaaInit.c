
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
#ifdef COMPOSITE
#include "cw.h"
#endif

#define MAX_PREALLOC_MEM	65536   /* MUST be >= 1024 */

#define MIN_OFFPIX_SIZE		(320*200)

static Bool XAACloseScreen(int i, ScreenPtr pScreen);
static void XAAGetImage(DrawablePtr pDrawable, int sx, int sy, int w, int h,
                        unsigned int format, unsigned long planemask,
                        char *pdstLine);
static void XAAGetSpans(DrawablePtr pDrawable, int wMax, DDXPointPtr ppt,
                        int *pwidth, int nspans, char *pdstStart);
static PixmapPtr XAACreatePixmap(ScreenPtr pScreen, int w, int h, int depth,
                                 unsigned usage_hint);
static Bool XAADestroyPixmap(PixmapPtr pPixmap);
static Bool XAAEnterVT(int index, int flags);
static void XAALeaveVT(int index, int flags);
static int XAASetDGAMode(int index, int num, DGADevicePtr devRet);
static void XAAEnableDisableFBAccess(int index, Bool enable);
static Bool XAAChangeWindowAttributes(WindowPtr pWin, unsigned long mask);

static DevPrivateKeyRec XAAScreenKeyRec;

#define XAAScreenKey (&XAAScreenKeyRec)
static DevPrivateKeyRec XAAGCKeyRec;

#define XAAGCKey (&XAAGCKeyRec)
static DevPrivateKeyRec XAAPixmapKeyRec;

#define XAAPixmapKey (&XAAPixmapKeyRec)

DevPrivateKey
XAAGetScreenKey(void)
{
    return XAAScreenKey;
}

DevPrivateKey
XAAGetGCKey(void)
{
    return XAAGCKey;
}

DevPrivateKey
XAAGetPixmapKey(void)
{
    return XAAPixmapKey;
}

/* temp kludge */
static Bool SwitchedOut = FALSE;

XAAInfoRecPtr
XAACreateInfoRec(void)
{
    XAAInfoRecPtr infoRec;

    infoRec = calloc(1, sizeof(XAAInfoRec));
    if (infoRec)
        infoRec->CachePixelGranularity = -1;

    return infoRec;
}

void
XAADestroyInfoRec(XAAInfoRecPtr infoRec)
{
    if (!infoRec)
        return;

    if (infoRec->ClosePixmapCache)
        (*infoRec->ClosePixmapCache) (infoRec->pScrn->pScreen);

    free(infoRec->PreAllocMem);

    free(infoRec->PixmapCachePrivate);

    free(infoRec);
}

Bool
XAAInit(ScreenPtr pScreen, XAAInfoRecPtr infoRec)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XAAScreenPtr pScreenPriv;
    int i;
    PictureScreenPtr ps = GetPictureScreenIfSet(pScreen);

    /* Return successfully if no acceleration wanted */
    if (!infoRec)
        return TRUE;

    if (!dixRegisterPrivateKey(&XAAGCKeyRec, PRIVATE_GC, sizeof(XAAGCRec)))
        return FALSE;

    if (!dixRegisterPrivateKey
        (&XAAPixmapKeyRec, PRIVATE_PIXMAP, sizeof(XAAPixmapRec)))
        return FALSE;

    if (!dixRegisterPrivateKey(&XAAScreenKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;

    if (!(pScreenPriv = malloc(sizeof(XAAScreenRec))))
        return FALSE;

    dixSetPrivate(&pScreen->devPrivates, XAAScreenKey, pScreenPriv);

    if (!xf86FBManagerRunning(pScreen))
        infoRec->Flags &= ~(PIXMAP_CACHE | OFFSCREEN_PIXMAPS);
    if (!(infoRec->Flags & LINEAR_FRAMEBUFFER))
        infoRec->Flags &= ~OFFSCREEN_PIXMAPS;

    if (!infoRec->FullPlanemask) {      /* for backwards compatibility */
        infoRec->FullPlanemask = (1 << pScrn->depth) - 1;
        infoRec->FullPlanemasks[pScrn->depth - 1] = infoRec->FullPlanemask;
    }

    for (i = 0; i < 32; i++) {
        if (!infoRec->FullPlanemasks[i])        /* keep any set by caller */
            infoRec->FullPlanemasks[i] = (1 << (i + 1)) - 1;
    }

    if (!XAAInitAccel(pScreen, infoRec))
        return FALSE;
    pScreenPriv->AccelInfoRec = infoRec;
    infoRec->ScratchGC.pScreen = pScreen;

    if (!infoRec->GetImage)
        infoRec->GetImage = XAAGetImage;
    if (!infoRec->GetSpans)
        infoRec->GetSpans = XAAGetSpans;
    if (!infoRec->CopyWindow)
        infoRec->CopyWindow = XAACopyWindow;

    pScreenPriv->CreateGC = pScreen->CreateGC;
    pScreen->CreateGC = XAACreateGC;
    pScreenPriv->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = XAACloseScreen;
    pScreenPriv->GetImage = pScreen->GetImage;
    pScreen->GetImage = infoRec->GetImage;
    pScreenPriv->GetSpans = pScreen->GetSpans;
    pScreen->GetSpans = infoRec->GetSpans;
    pScreenPriv->CopyWindow = pScreen->CopyWindow;
    pScreen->CopyWindow = infoRec->CopyWindow;
    pScreenPriv->CreatePixmap = pScreen->CreatePixmap;
    pScreen->CreatePixmap = XAACreatePixmap;
    pScreenPriv->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = XAADestroyPixmap;
    pScreenPriv->ChangeWindowAttributes = pScreen->ChangeWindowAttributes;
    pScreen->ChangeWindowAttributes = XAAChangeWindowAttributes;

    pScreenPriv->EnterVT = pScrn->EnterVT;
    pScrn->EnterVT = XAAEnterVT;
    pScreenPriv->LeaveVT = pScrn->LeaveVT;
    pScrn->LeaveVT = XAALeaveVT;
    pScreenPriv->SetDGAMode = pScrn->SetDGAMode;
    pScrn->SetDGAMode = XAASetDGAMode;
    pScreenPriv->EnableDisableFBAccess = pScrn->EnableDisableFBAccess;
    pScrn->EnableDisableFBAccess = XAAEnableDisableFBAccess;

    pScreenPriv->WindowExposures = pScreen->WindowExposures;
    if (ps) {
        pScreenPriv->Composite = ps->Composite;
        ps->Composite = XAAComposite;
        pScreenPriv->Glyphs = ps->Glyphs;
        ps->Glyphs = XAAGlyphs;
    }
    if (pScrn->overlayFlags & OVERLAY_8_32_PLANAR)
        XAASetupOverlay8_32Planar(pScreen);

    infoRec->PreAllocMem = malloc(MAX_PREALLOC_MEM);
    if (infoRec->PreAllocMem)
        infoRec->PreAllocSize = MAX_PREALLOC_MEM;

    if (infoRec->Flags & PIXMAP_CACHE)
        xf86RegisterFreeBoxCallback(pScreen, infoRec->InitPixmapCache,
                                    (pointer) infoRec);

    if (infoRec->Flags & MICROSOFT_ZERO_LINE_BIAS)
        miSetZeroLineBias(pScreen, OCTANT1 | OCTANT2 | OCTANT3 | OCTANT4);

#ifdef COMPOSITE
    /* Initialize the composite wrapper.  This needs to happen after the
     * wrapping above (so it comes before us), but before all other extensions,
     * so it doesn't confuse them. (particularly damage).
     */
    miInitializeCompositeWrapper(pScreen);
#endif

    return TRUE;
}

static Bool
XAACloseScreen(int i, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XAAScreenPtr pScreenPriv =
        (XAAScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XAAScreenKey);

    pScrn->EnterVT = pScreenPriv->EnterVT;
    pScrn->LeaveVT = pScreenPriv->LeaveVT;
    pScrn->EnableDisableFBAccess = pScreenPriv->EnableDisableFBAccess;

    pScreen->CreateGC = pScreenPriv->CreateGC;
    pScreen->CloseScreen = pScreenPriv->CloseScreen;
    pScreen->GetImage = pScreenPriv->GetImage;
    pScreen->GetSpans = pScreenPriv->GetSpans;
    pScreen->CopyWindow = pScreenPriv->CopyWindow;
    pScreen->WindowExposures = pScreenPriv->WindowExposures;
    pScreen->CreatePixmap = pScreenPriv->CreatePixmap;
    pScreen->DestroyPixmap = pScreenPriv->DestroyPixmap;
    pScreen->ChangeWindowAttributes = pScreenPriv->ChangeWindowAttributes;

    /* We leave it up to the client to free the XAAInfoRec */

    free((pointer) pScreenPriv);

    return (*pScreen->CloseScreen) (i, pScreen);
}

static void
XAAGetImage(DrawablePtr pDraw,
            int sx, int sy, int w, int h,
            unsigned int format, unsigned long planemask, char *pdstLine)
{
    ScreenPtr pScreen = pDraw->pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    ScrnInfoPtr pScrn = infoRec->pScrn;

    if (pScrn->vtSema &&
        ((pDraw->type == DRAWABLE_WINDOW) || IS_OFFSCREEN_PIXMAP(pDraw))) {
        if (infoRec->ReadPixmap && (format == ZPixmap) &&
            ((planemask & infoRec->FullPlanemasks[pDraw->depth - 1]) ==
             infoRec->FullPlanemasks[pDraw->depth - 1]) &&
            (pDraw->bitsPerPixel == BitsPerPixel(pDraw->depth))) {
            (*infoRec->ReadPixmap) (pScrn,
                                    sx + pDraw->x, sy + pDraw->y, w, h,
                                    (unsigned char *) pdstLine,
                                    PixmapBytePad(w, pDraw->depth),
                                    pDraw->bitsPerPixel, pDraw->depth);
            return;
        }
        SYNC_CHECK(pDraw);
    }

    XAA_SCREEN_PROLOGUE(pScreen, GetImage);
    (*pScreen->GetImage) (pDraw, sx, sy, w, h, format, planemask, pdstLine);
    XAA_SCREEN_EPILOGUE(pScreen, GetImage, XAAGetImage);
}

static void
XAAGetSpans(DrawablePtr pDraw,
            int wMax, DDXPointPtr ppt, int *pwidth, int nspans, char *pdstStart)
{
    ScreenPtr pScreen = pDraw->pScreen;

    XAA_SCREEN_PROLOGUE(pScreen, GetSpans);
    if (xf86Screens[pScreen->myNum]->vtSema &&
        ((pDraw->type == DRAWABLE_WINDOW) || IS_OFFSCREEN_PIXMAP(pDraw))) {
        SYNC_CHECK(pDraw);
    }
    (*pScreen->GetSpans) (pDraw, wMax, ppt, pwidth, nspans, pdstStart);
    XAA_SCREEN_EPILOGUE(pScreen, GetSpans, XAAGetSpans);
}

static int
XAAPixmapBPP(ScreenPtr pScreen, int depth)
{
    PixmapPtr pPix;
    int bpp;
    DestroyPixmapProcPtr destroyPixmap;

    XAA_SCREEN_PROLOGUE(pScreen, CreatePixmap);
    pPix = (*pScreen->CreatePixmap) (pScreen, 1, 1, depth,
                                     CREATE_PIXMAP_USAGE_SCRATCH);
    XAA_SCREEN_EPILOGUE(pScreen, CreatePixmap, XAACreatePixmap);
    if (!pPix)
        return 0;
    bpp = pPix->drawable.bitsPerPixel;
    destroyPixmap = pScreen->DestroyPixmap;
    XAA_SCREEN_PROLOGUE(pScreen, DestroyPixmap);
    (*pScreen->DestroyPixmap) (pPix);
    XAA_SCREEN_EPILOGUE(pScreen, DestroyPixmap, destroyPixmap);
    return bpp;
}

static void
XAAInitializeOffscreenDepths(ScreenPtr pScreen)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    int d, dep;

    infoRec->offscreenDepthsInitialized = TRUE;
    infoRec->offscreenDepths = 0;
    if (infoRec->Flags & OFFSCREEN_PIXMAPS) {
        for (d = 0; d < pScreen->numDepths; d++) {
            dep = pScreen->allowedDepths[d].depth;
            if (XAAPixmapBPP(pScreen, dep) == pScrn->bitsPerPixel)
                infoRec->offscreenDepths |= (1 << (dep - 1));
        }
    }
}

static PixmapPtr
XAACreatePixmap(ScreenPtr pScreen, int w, int h, int depth, unsigned usage_hint)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XAAPixmapPtr pPriv;
    PixmapPtr pPix = NULL;
    int size = w * h;

    if (w > 32767 || h > 32767)
        return NullPixmap;

    if (!infoRec->offscreenDepthsInitialized)
        XAAInitializeOffscreenDepths(pScreen);

    if (pScrn->vtSema &&
        (usage_hint != CREATE_PIXMAP_USAGE_GLYPH_PICTURE) &&
        (infoRec->offscreenDepths & (1 << (depth - 1))) &&
        (size >= MIN_OFFPIX_SIZE) && !SwitchedOut &&
        (!infoRec->maxOffPixWidth || (w <= infoRec->maxOffPixWidth)) &&
        (!infoRec->maxOffPixHeight || (h <= infoRec->maxOffPixHeight))) {
        PixmapLinkPtr pLink;
        PixmapPtr pScreenPix;
        FBAreaPtr area;
        int gran = 0;

        switch (pScrn->bitsPerPixel) {
        case 24:
        case 8:
            gran = 4;
            break;
        case 16:
            gran = 2;
            break;
        case 32:
            gran = 1;
            break;
        default:
            break;
        }

        if (BITMAP_SCANLINE_PAD == 64)
            gran *= 2;

        if (!(area = xf86AllocateOffscreenArea(pScreen, w, h, gran, 0,
                                               XAARemoveAreaCallback, NULL))) {
            goto BAILOUT;
        }

        if (!(pLink = malloc(sizeof(PixmapLink)))) {
            xf86FreeOffscreenArea(area);
            goto BAILOUT;
        }

        XAA_SCREEN_PROLOGUE(pScreen, CreatePixmap);
        pPix = (*pScreen->CreatePixmap) (pScreen, 0, 0, depth, usage_hint);
        XAA_SCREEN_EPILOGUE(pScreen, CreatePixmap, XAACreatePixmap);

        if (!pPix) {
            free(pLink);
            xf86FreeOffscreenArea(area);
            goto BAILOUT;
        }

        pScreenPix = (*pScreen->GetScreenPixmap) (pScreen);

        pPriv = XAA_GET_PIXMAP_PRIVATE(pPix);
        pPix->drawable.x = area->box.x1;
        pPix->drawable.y = area->box.y1;
        pPix->drawable.width = w;
        pPix->drawable.height = h;
        pPix->drawable.bitsPerPixel = pScrn->bitsPerPixel;
        pPix->devKind = pScreenPix->devKind;
        pPix->devPrivate.ptr = pScreenPix->devPrivate.ptr;
        area->devPrivate.ptr = pPix;

        pPriv->flags = OFFSCREEN;
        pPriv->offscreenArea = area;
        pPriv->freeData = FALSE;

        pLink->next = infoRec->OffscreenPixmaps;
        pLink->pPix = pPix;
        infoRec->OffscreenPixmaps = pLink;
        return pPix;
    }
 BAILOUT:
    XAA_SCREEN_PROLOGUE(pScreen, CreatePixmap);
    pPix = (*pScreen->CreatePixmap) (pScreen, w, h, depth, usage_hint);
    XAA_SCREEN_EPILOGUE(pScreen, CreatePixmap, XAACreatePixmap);

    if (pPix) {
        pPriv = XAA_GET_PIXMAP_PRIVATE(pPix);
        pPriv->flags = 0;
        pPriv->offscreenArea = NULL;
        pPriv->freeData = FALSE;
        if (!w || !h)           /* either scratch or shared memory */
            pPriv->flags |= SHARED_PIXMAP;
    }

    return pPix;
}

static Bool
XAADestroyPixmap(PixmapPtr pPix)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pPix);
    Bool ret;

    if (pPix->refcnt == 1) {
        if (pPriv->flags & OFFSCREEN) {
            if (pPriv->flags & DGA_PIXMAP)
                free(pPriv->offscreenArea);
            else {
                FBAreaPtr area = pPriv->offscreenArea;
                PixmapLinkPtr pLink = infoRec->OffscreenPixmaps;
                PixmapLinkPtr prev = NULL;

                while (pLink->pPix != pPix) {
                    prev = pLink;
                    pLink = pLink->next;
                }

                if (prev)
                    prev->next = pLink->next;
                else
                    infoRec->OffscreenPixmaps = pLink->next;

                if (!area)
                    area = pLink->area;

                xf86FreeOffscreenArea(area);
                pPriv->offscreenArea = NULL;
                free(pLink);
            }
        }

        if (pPriv->freeData) {  /* pixmaps that were once in video ram */
            free(pPix->devPrivate.ptr);
            pPix->devPrivate.ptr = NULL;
        }
    }

    XAA_SCREEN_PROLOGUE(pScreen, DestroyPixmap);
    ret = (*pScreen->DestroyPixmap) (pPix);
    XAA_SCREEN_EPILOGUE(pScreen, DestroyPixmap, XAADestroyPixmap);

    return ret;
}

static Bool
XAAChangeWindowAttributes(WindowPtr pWin, unsigned long mask)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    Bool ret;

    XAA_SCREEN_PROLOGUE(pScreen, ChangeWindowAttributes);
    ret = (*pScreen->ChangeWindowAttributes) (pWin, mask);
    XAA_SCREEN_EPILOGUE(pScreen, ChangeWindowAttributes,
                        XAAChangeWindowAttributes);

    /* we have to assume that shared memory pixmaps are dirty
       because we can't wrap operations on them */

    if ((mask & CWBackPixmap) && (pWin->backgroundState == BackgroundPixmap) &&
        PIXMAP_IS_SHARED(pWin->background.pixmap)) {
        XAAPixmapPtr pPixPriv = XAA_GET_PIXMAP_PRIVATE(pWin->background.pixmap);

        pPixPriv->flags |= DIRTY;
    }
    if ((mask & CWBorderPixmap) && !(pWin->borderIsPixel) &&
        PIXMAP_IS_SHARED(pWin->border.pixmap)) {
        XAAPixmapPtr pPixPriv = XAA_GET_PIXMAP_PRIVATE(pWin->border.pixmap);

        pPixPriv->flags |= DIRTY;
    }

    return ret;
}

/*  These two aren't really needed for anything */

static Bool
XAAEnterVT(int index, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[index];
    Bool ret;
    ScreenPtr pScreen = screenInfo.screens[index];
    XAAScreenPtr pScreenPriv =
        (XAAScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XAAScreenKey);

    pScrn->EnterVT = pScreenPriv->EnterVT;
    ret = ((*pScreenPriv->EnterVT) (index, flags));
    pScreenPriv->EnterVT = pScrn->EnterVT;
    pScrn->EnterVT = XAAEnterVT;
    return ret;
}

static void
XAALeaveVT(int index, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[index];
    ScreenPtr pScreen = screenInfo.screens[index];
    XAAScreenPtr pScreenPriv =
        (XAAScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XAAScreenKey);
    XAAInfoRecPtr infoRec = pScreenPriv->AccelInfoRec;

    if (infoRec->NeedToSync) {
        (*infoRec->Sync) (infoRec->pScrn);
        infoRec->NeedToSync = FALSE;
    }

    pScrn->LeaveVT = pScreenPriv->LeaveVT;
    (*pScreenPriv->LeaveVT) (index, flags);
    pScreenPriv->LeaveVT = pScrn->LeaveVT;
    pScrn->LeaveVT = XAALeaveVT;
}

typedef struct {
    Bool UsingPixmapCache;
    Bool CanDoColor8x8;
    Bool CanDoMono8x8;
} SavedCacheState, *SavedCacheStatePtr;

static int
XAASetDGAMode(int index, int num, DGADevicePtr devRet)
{
    ScreenPtr pScreen = screenInfo.screens[index];
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    XAAScreenPtr pScreenPriv =
        (XAAScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XAAScreenKey);
    int ret;

    if (!num && infoRec->dgaSaves) {    /* restore old pixmap cache state */
        SavedCacheStatePtr state = (SavedCacheStatePtr) infoRec->dgaSaves;

        infoRec->UsingPixmapCache = state->UsingPixmapCache;
        infoRec->CanDoColor8x8 = state->CanDoColor8x8;
        infoRec->CanDoMono8x8 = state->CanDoMono8x8;
        free(infoRec->dgaSaves);
        infoRec->dgaSaves = NULL;
    }

    ret = (*pScreenPriv->SetDGAMode) (index, num, devRet);
    if (ret != Success)
        return ret;

    if (num && devRet->pPix) {  /* accelerate this pixmap */
        XAAPixmapPtr pixPriv = XAA_GET_PIXMAP_PRIVATE(devRet->pPix);
        FBAreaPtr area;

        if ((area = malloc(sizeof(FBArea)))) {
            area->pScreen = pScreen;
            area->box.x1 = 0;
            area->box.x2 = 0;
            area->box.y1 = devRet->mode->pixmapWidth;
            area->box.y2 = devRet->mode->pixmapHeight;
            area->granularity = 0;
            area->MoveAreaCallback = 0;
            area->RemoveAreaCallback = 0;
            area->devPrivate.ptr = 0;

            pixPriv->flags |= OFFSCREEN | DGA_PIXMAP;
            pixPriv->offscreenArea = area;

            if (!infoRec->dgaSaves) {   /* save pixmap cache state */
                SavedCacheStatePtr state = malloc(sizeof(SavedCacheState));

                state->UsingPixmapCache = infoRec->UsingPixmapCache;
                state->CanDoColor8x8 = infoRec->CanDoColor8x8;
                state->CanDoMono8x8 = infoRec->CanDoMono8x8;
                infoRec->dgaSaves = (char *) state;

                infoRec->UsingPixmapCache = FALSE;
                if (infoRec->PixmapCacheFlags & CACHE_MONO_8x8)
                    infoRec->CanDoMono8x8 = FALSE;
                if (infoRec->PixmapCacheFlags & CACHE_COLOR_8x8)
                    infoRec->CanDoColor8x8 = FALSE;
            }
        }
    }

    return ret;
}

static void
XAAEnableDisableFBAccess(int index, Bool enable)
{
    ScreenPtr pScreen = screenInfo.screens[index];
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    XAAScreenPtr pScreenPriv =
        (XAAScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XAAScreenKey);

    if (!enable) {
        if ((infoRec->Flags & OFFSCREEN_PIXMAPS) && (infoRec->OffscreenPixmaps))
            XAAMoveOutOffscreenPixmaps(pScreen);
        if (infoRec->Flags & PIXMAP_CACHE)
            XAAInvalidatePixmapCache(pScreen);
        SwitchedOut = TRUE;
    }

    (*pScreenPriv->EnableDisableFBAccess) (index, enable);

    if (enable) {
        if ((infoRec->Flags & OFFSCREEN_PIXMAPS) && (infoRec->OffscreenPixmaps))
            XAAMoveInOffscreenPixmaps(pScreen);
        SwitchedOut = FALSE;
    }
}
