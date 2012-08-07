/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2004 Nokia 
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Nokia not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Nokia makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * NOKIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL NOKIA BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "ephyr.h"

#include "inputstr.h"
#include "scrnintstr.h"
#include "ephyrlog.h"

#ifdef XF86DRI
#include "ephyrdri.h"
#include "ephyrdriext.h"
#include "ephyrglxext.h"
#endif                          /* XF86DRI */

#include "xkbsrv.h"

extern int KdTsPhyScreen;

KdKeyboardInfo *ephyrKbd;
KdPointerInfo *ephyrMouse;
EphyrKeySyms ephyrKeySyms;
Bool ephyrNoDRI = FALSE;
Bool ephyrNoXV = FALSE;

static int mouseState = 0;
static Rotation ephyrRandr = RR_Rotate_0;

typedef struct _EphyrInputPrivate {
    Bool enabled;
} EphyrKbdPrivate, EphyrPointerPrivate;

Bool EphyrWantGrayScale = 0;

Bool
ephyrInitialize(KdCardInfo * card, EphyrPriv * priv)
{
    OsSignal(SIGUSR1, hostx_handle_signal);

    priv->base = 0;
    priv->bytes_per_line = 0;
    return TRUE;
}

Bool
ephyrCardInit(KdCardInfo * card)
{
    EphyrPriv *priv;

    priv = (EphyrPriv *) malloc(sizeof(EphyrPriv));
    if (!priv)
        return FALSE;

    if (!ephyrInitialize(card, priv)) {
        free(priv);
        return FALSE;
    }
    card->driver = priv;

    return TRUE;
}

Bool
ephyrScreenInitialize(KdScreenInfo * screen, EphyrScrPriv * scrpriv)
{
    int width = 640, height = 480;
    CARD32 redMask, greenMask, blueMask;

    if (hostx_want_screen_size(screen, &width, &height)
        || !screen->width || !screen->height) {
        screen->width = width;
        screen->height = height;
    }

    if (EphyrWantGrayScale)
        screen->fb.depth = 8;

    if (screen->fb.depth && screen->fb.depth != hostx_get_depth()) {
        if (screen->fb.depth < hostx_get_depth()
            && (screen->fb.depth == 24 || screen->fb.depth == 16
                || screen->fb.depth == 8)) {
            hostx_set_server_depth(screen, screen->fb.depth);
        }
        else
            ErrorF
                ("\nXephyr: requested screen depth not supported, setting to match hosts.\n");
    }

    screen->fb.depth = hostx_get_server_depth(screen);
    screen->rate = 72;

    if (screen->fb.depth <= 8) {
        if (EphyrWantGrayScale)
            screen->fb.visuals = ((1 << StaticGray) | (1 << GrayScale));
        else
            screen->fb.visuals = ((1 << StaticGray) |
                                  (1 << GrayScale) |
                                  (1 << StaticColor) |
                                  (1 << PseudoColor) |
                                  (1 << TrueColor) | (1 << DirectColor));

        screen->fb.redMask = 0x00;
        screen->fb.greenMask = 0x00;
        screen->fb.blueMask = 0x00;
        screen->fb.depth = 8;
        screen->fb.bitsPerPixel = 8;
    }
    else {
        screen->fb.visuals = (1 << TrueColor);

        if (screen->fb.depth <= 15) {
            screen->fb.depth = 15;
            screen->fb.bitsPerPixel = 16;
        }
        else if (screen->fb.depth <= 16) {
            screen->fb.depth = 16;
            screen->fb.bitsPerPixel = 16;
        }
        else if (screen->fb.depth <= 24) {
            screen->fb.depth = 24;
            screen->fb.bitsPerPixel = 32;
        }
        else if (screen->fb.depth <= 30) {
            screen->fb.depth = 30;
            screen->fb.bitsPerPixel = 32;
        }
        else {
            ErrorF("\nXephyr: Unsupported screen depth %d\n", screen->fb.depth);
            return FALSE;
        }

        hostx_get_visual_masks(screen, &redMask, &greenMask, &blueMask);

        screen->fb.redMask = (Pixel) redMask;
        screen->fb.greenMask = (Pixel) greenMask;
        screen->fb.blueMask = (Pixel) blueMask;

    }

    scrpriv->randr = screen->randr;

    return ephyrMapFramebuffer(screen);
}

Bool
ephyrScreenInit(KdScreenInfo * screen)
{
    EphyrScrPriv *scrpriv;

    scrpriv = calloc(1, sizeof(EphyrScrPriv));

    if (!scrpriv)
        return FALSE;

    screen->driver = scrpriv;

    if (!ephyrScreenInitialize(screen, scrpriv)) {
        screen->driver = 0;
        free(scrpriv);
        return FALSE;
    }

    return TRUE;
}

void *
ephyrWindowLinear(ScreenPtr pScreen,
                  CARD32 row,
                  CARD32 offset, int mode, CARD32 *size, void *closure)
{
    KdScreenPriv(pScreen);
    EphyrPriv *priv = pScreenPriv->card->driver;

    if (!pScreenPriv->enabled)
        return 0;

    *size = priv->bytes_per_line;
    return priv->base + row * priv->bytes_per_line + offset;
}

/**
 * Figure out display buffer size. If fakexa is enabled, allocate a larger
 * buffer so that fakexa has space to put offscreen pixmaps.
 */
int
ephyrBufferHeight(KdScreenInfo * screen)
{
    int buffer_height;

    if (ephyrFuncs.initAccel == NULL)
        buffer_height = screen->height;
    else
        buffer_height = 3 * screen->height;
    return buffer_height;
}

Bool
ephyrMapFramebuffer(KdScreenInfo * screen)
{
    EphyrScrPriv *scrpriv = screen->driver;
    EphyrPriv *priv = screen->card->driver;
    KdPointerMatrix m;
    int buffer_height;

    EPHYR_LOG("screen->width: %d, screen->height: %d index=%d",
              screen->width, screen->height, screen->mynum);

    /*
     * Use the rotation last applied to ourselves (in the Xephyr case the fb
     * coordinate system moves independently of the pointer coordiante system).
     */
    KdComputePointerMatrix(&m, ephyrRandr, screen->width, screen->height);
    KdSetPointerMatrix(&m);

    priv->bytes_per_line =
        ((screen->width * screen->fb.bitsPerPixel + 31) >> 5) << 2;

    buffer_height = ephyrBufferHeight(screen);

    priv->base =
        hostx_screen_init(screen, screen->width, screen->height, buffer_height);

    if ((scrpriv->randr & RR_Rotate_0) && !(scrpriv->randr & RR_Reflect_All)) {
        scrpriv->shadow = FALSE;

        screen->fb.byteStride = priv->bytes_per_line;
        screen->fb.pixelStride = screen->width;
        screen->fb.frameBuffer = (CARD8 *) (priv->base);
    }
    else {
        /* Rotated/Reflected so we need to use shadow fb */
        scrpriv->shadow = TRUE;

        EPHYR_LOG("allocing shadow");

        KdShadowFbAlloc(screen,
                        scrpriv->randr & (RR_Rotate_90 | RR_Rotate_270));
    }

    return TRUE;
}

void
ephyrSetScreenSizes(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    EphyrScrPriv *scrpriv = screen->driver;

    if (scrpriv->randr & (RR_Rotate_0 | RR_Rotate_180)) {
        pScreen->width = screen->width;
        pScreen->height = screen->height;
        pScreen->mmWidth = screen->width_mm;
        pScreen->mmHeight = screen->height_mm;
    }
    else {
        pScreen->width = screen->height;
        pScreen->height = screen->width;
        pScreen->mmWidth = screen->height_mm;
        pScreen->mmHeight = screen->width_mm;
    }
}

Bool
ephyrUnmapFramebuffer(KdScreenInfo * screen)
{
    EphyrScrPriv *scrpriv = screen->driver;

    if (scrpriv->shadow)
        KdShadowFbFree(screen);

    /* Note, priv->base will get freed when XImage recreated */

    return TRUE;
}

void
ephyrShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;

    EPHYR_LOG("slow paint");

    /* FIXME: Slow Rotated/Reflected updates could be much
     * much faster efficiently updating via tranforming 
     * pBuf->pDamage  regions     
     */
    shadowUpdateRotatePacked(pScreen, pBuf);
    hostx_paint_rect(screen, 0, 0, 0, 0, screen->width, screen->height);
}

static void
ephyrInternalDamageRedisplay(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    EphyrScrPriv *scrpriv = screen->driver;
    RegionPtr pRegion;

    if (!scrpriv || !scrpriv->pDamage)
        return;

    pRegion = DamageRegion(scrpriv->pDamage);

    if (RegionNotEmpty(pRegion)) {
        int nbox;
        BoxPtr pbox;

        nbox = RegionNumRects(pRegion);
        pbox = RegionRects(pRegion);

        while (nbox--) {
            hostx_paint_rect(screen,
                             pbox->x1, pbox->y1,
                             pbox->x1, pbox->y1,
                             pbox->x2 - pbox->x1, pbox->y2 - pbox->y1);
            pbox++;
        }
        DamageEmpty(scrpriv->pDamage);
    }
}

static void
ephyrInternalDamageBlockHandler(pointer data, OSTimePtr pTimeout, pointer pRead)
{
    ScreenPtr pScreen = (ScreenPtr) data;

    ephyrInternalDamageRedisplay(pScreen);
}

static void
ephyrInternalDamageWakeupHandler(pointer data, int i, pointer LastSelectMask)
{
    /* FIXME: Not needed ? */
}

Bool
ephyrSetInternalDamage(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    EphyrScrPriv *scrpriv = screen->driver;
    PixmapPtr pPixmap = NULL;

    scrpriv->pDamage = DamageCreate((DamageReportFunc) 0,
                                    (DamageDestroyFunc) 0,
                                    DamageReportNone, TRUE, pScreen, pScreen);

    if (!RegisterBlockAndWakeupHandlers(ephyrInternalDamageBlockHandler,
                                        ephyrInternalDamageWakeupHandler,
                                        (pointer) pScreen))
        return FALSE;

    pPixmap = (*pScreen->GetScreenPixmap) (pScreen);

    DamageRegister(&pPixmap->drawable, scrpriv->pDamage);

    return TRUE;
}

void
ephyrUnsetInternalDamage(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    EphyrScrPriv *scrpriv = screen->driver;
    PixmapPtr pPixmap = NULL;

    pPixmap = (*pScreen->GetScreenPixmap) (pScreen);
    DamageUnregister(&pPixmap->drawable, scrpriv->pDamage);
    DamageDestroy(scrpriv->pDamage);

    RemoveBlockAndWakeupHandlers(ephyrInternalDamageBlockHandler,
                                 ephyrInternalDamageWakeupHandler,
                                 (pointer) pScreen);
}

#ifdef RANDR
Bool
ephyrRandRGetInfo(ScreenPtr pScreen, Rotation * rotations)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    EphyrScrPriv *scrpriv = screen->driver;
    RRScreenSizePtr pSize;
    Rotation randr;
    int n = 0;

    struct {
        int width, height;
    } sizes[] = {
        {1600, 1200},
        {1400, 1050},
        {1280, 960},
        {1280, 1024},
        {1152, 864},
        {1024, 768},
        {832, 624},
        {800, 600},
        {720, 400},
        {480, 640},
        {640, 480},
        {640, 400},
        {320, 240},
        {240, 320},
        {160, 160},
        {0, 0}
    };

    EPHYR_LOG("mark");

    *rotations = RR_Rotate_All | RR_Reflect_All;

    if (!hostx_want_preexisting_window(screen)
        && !hostx_want_fullscreen()) {  /* only if no -parent switch */
        while (sizes[n].width != 0 && sizes[n].height != 0) {
            RRRegisterSize(pScreen,
                           sizes[n].width,
                           sizes[n].height,
                           (sizes[n].width * screen->width_mm) / screen->width,
                           (sizes[n].height * screen->height_mm) /
                           screen->height);
            n++;
        }
    }

    pSize = RRRegisterSize(pScreen,
                           screen->width,
                           screen->height, screen->width_mm, screen->height_mm);

    randr = KdSubRotation(scrpriv->randr, screen->randr);

    RRSetCurrentConfig(pScreen, randr, 0, pSize);

    return TRUE;
}

Bool
ephyrRandRSetConfig(ScreenPtr pScreen,
                    Rotation randr, int rate, RRScreenSizePtr pSize)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    EphyrScrPriv *scrpriv = screen->driver;
    Bool wasEnabled = pScreenPriv->enabled;
    EphyrScrPriv oldscr;
    int oldwidth, oldheight, oldmmwidth, oldmmheight;
    Bool oldshadow;
    int newwidth, newheight;

    if (screen->randr & (RR_Rotate_0 | RR_Rotate_180)) {
        newwidth = pSize->width;
        newheight = pSize->height;
    }
    else {
        newwidth = pSize->height;
        newheight = pSize->width;
    }

    if (wasEnabled)
        KdDisableScreen(pScreen);

    oldscr = *scrpriv;

    oldwidth = screen->width;
    oldheight = screen->height;
    oldmmwidth = pScreen->mmWidth;
    oldmmheight = pScreen->mmHeight;
    oldshadow = scrpriv->shadow;

    /*
     * Set new configuration
     */

    /*
     * We need to store the rotation value for pointer coords transformation;
     * though initially the pointer and fb rotation are identical, when we map
     * the fb, the screen will be reinitialized and return into an unrotated
     * state (presumably the HW is taking care of the rotation of the fb), but the
     * pointer still needs to be transformed.
     */
    ephyrRandr = KdAddRotation(screen->randr, randr);
    scrpriv->randr = ephyrRandr;

    ephyrUnmapFramebuffer(screen);

    screen->width = newwidth;
    screen->height = newheight;

    if (!ephyrMapFramebuffer(screen))
        goto bail4;

    /* FIXME below should go in own call */

    if (oldshadow)
        KdShadowUnset(screen->pScreen);
    else
        ephyrUnsetInternalDamage(screen->pScreen);

    if (scrpriv->shadow) {
        if (!KdShadowSet(screen->pScreen,
                         scrpriv->randr, ephyrShadowUpdate, ephyrWindowLinear))
            goto bail4;
    }
    else {
        /* Without shadow fb ( non rotated ) we need 
         * to use damage to efficiently update display
         * via signal regions what to copy from 'fb'.
         */
        if (!ephyrSetInternalDamage(screen->pScreen))
            goto bail4;
    }

    ephyrSetScreenSizes(screen->pScreen);

    /*
     * Set frame buffer mapping
     */
    (*pScreen->ModifyPixmapHeader) (fbGetScreenPixmap(pScreen),
                                    pScreen->width,
                                    pScreen->height,
                                    screen->fb.depth,
                                    screen->fb.bitsPerPixel,
                                    screen->fb.byteStride,
                                    screen->fb.frameBuffer);

    /* set the subpixel order */

    KdSetSubpixelOrder(pScreen, scrpriv->randr);

    if (wasEnabled)
        KdEnableScreen(pScreen);

    RRScreenSizeNotify(pScreen);

    return TRUE;

 bail4:
    EPHYR_LOG("bailed");

    ephyrUnmapFramebuffer(screen);
    *scrpriv = oldscr;
    (void) ephyrMapFramebuffer(screen);

    pScreen->width = oldwidth;
    pScreen->height = oldheight;
    pScreen->mmWidth = oldmmwidth;
    pScreen->mmHeight = oldmmheight;

    if (wasEnabled)
        KdEnableScreen(pScreen);
    return FALSE;
}

Bool
ephyrRandRInit(ScreenPtr pScreen)
{
    rrScrPrivPtr pScrPriv;

    if (!RRScreenInit(pScreen))
        return FALSE;

    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo = ephyrRandRGetInfo;
    pScrPriv->rrSetConfig = ephyrRandRSetConfig;
    return TRUE;
}

static Bool
ephyrResizeScreen (ScreenPtr           pScreen,
                  int                  newwidth,
                  int                  newheight)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    RRScreenSize size = {0};
    Bool ret;
    int t;

    if (screen->randr & (RR_Rotate_90|RR_Rotate_270)) {
        t = newwidth;
        newwidth = newheight;
        newheight = t;
    }

    if (newwidth == screen->width && newheight == screen->height) {
        return FALSE;
    }

    size.width = newwidth;
    size.height = newheight;

    ret = ephyrRandRSetConfig (pScreen, screen->randr, 0, &size);
    if (ret) {
        RROutputPtr output;

        output = RRFirstOutput(pScreen);
        if (!output)
            return FALSE;
        RROutputSetModes(output, NULL, 0, 0);
    }

    return ret;
}
#endif

Bool
ephyrCreateColormap(ColormapPtr pmap)
{
    return fbInitializeColormap(pmap);
}

Bool
ephyrInitScreen(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;

    EPHYR_LOG("pScreen->myNum:%d\n", pScreen->myNum);
    hostx_set_screen_number(screen, pScreen->myNum);
    hostx_set_win_title(screen, "(ctrl+shift grabs mouse and keyboard)");
    pScreen->CreateColormap = ephyrCreateColormap;

#ifdef XV
    if (!ephyrNoXV) {
        if (!ephyrInitVideo(pScreen)) {
            EPHYR_LOG_ERROR("failed to initialize xvideo\n");
        }
        else {
            EPHYR_LOG("initialized xvideo okay\n");
        }
    }
#endif /*XV*/
#ifdef XF86DRI
    if (!ephyrNoDRI && !hostx_has_dri()) {
        EPHYR_LOG("host x does not support DRI. Disabling DRI forwarding\n");
        ephyrNoDRI = TRUE;
    }
    if (!ephyrNoDRI) {
        ephyrDRIExtensionInit(pScreen);
        ephyrHijackGLXExtension();
    }
#endif

    return TRUE;
}

Bool
ephyrFinishInitScreen(ScreenPtr pScreen)
{
    /* FIXME: Calling this even if not using shadow.  
     * Seems harmless enough. But may be safer elsewhere.
     */
    if (!shadowSetup(pScreen))
        return FALSE;

#ifdef RANDR
    if (!ephyrRandRInit(pScreen))
        return FALSE;
#endif

    return TRUE;
}

Bool
ephyrCreateResources(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    EphyrScrPriv *scrpriv = screen->driver;

    EPHYR_LOG("mark pScreen=%p mynum=%d shadow=%d",
              pScreen, pScreen->myNum, scrpriv->shadow);

    if (scrpriv->shadow)
        return KdShadowSet(pScreen,
                           scrpriv->randr,
                           ephyrShadowUpdate, ephyrWindowLinear);
    else
        return ephyrSetInternalDamage(pScreen);
}

void
ephyrPreserve(KdCardInfo * card)
{
}

Bool
ephyrEnable(ScreenPtr pScreen)
{
    return TRUE;
}

Bool
ephyrDPMS(ScreenPtr pScreen, int mode)
{
    return TRUE;
}

void
ephyrDisable(ScreenPtr pScreen)
{
}

void
ephyrRestore(KdCardInfo * card)
{
}

void
ephyrScreenFini(KdScreenInfo * screen)
{
    EphyrScrPriv *scrpriv = screen->driver;

    if (scrpriv->shadow) {
        KdShadowFbFree(screen);
    }
    free(screen->driver);
    screen->driver = NULL;
}

/*  
 * Port of Mark McLoughlin's Xnest fix for focus in + modifier bug.
 * See https://bugs.freedesktop.org/show_bug.cgi?id=3030
 */
void
ephyrUpdateModifierState(unsigned int state)
{

    DeviceIntPtr pDev = inputInfo.keyboard;
    KeyClassPtr keyc = pDev->key;
    int i;
    CARD8 mask;
    int xkb_state;

    if (!pDev)
        return;

    xkb_state = XkbStateFieldFromRec(&pDev->key->xkbInfo->state);
    state = state & 0xff;

    if (xkb_state == state)
        return;

    for (i = 0, mask = 1; i < 8; i++, mask <<= 1) {
        int key;

        /* Modifier is down, but shouldn't be
         */
        if ((xkb_state & mask) && !(state & mask)) {
            int count = keyc->modifierKeyCount[i];

            for (key = 0; key < MAP_LENGTH; key++)
                if (keyc->xkbInfo->desc->map->modmap[key] & mask) {
                    if (key_is_down(pDev, key, KEY_PROCESSED))
                        KdEnqueueKeyboardEvent(ephyrKbd, key, TRUE);

                    if (--count == 0)
                        break;
                }
        }

        /* Modifier shoud be down, but isn't
         */
        if (!(xkb_state & mask) && (state & mask))
            for (key = 0; key < MAP_LENGTH; key++)
                if (keyc->xkbInfo->desc->map->modmap[key] & mask) {
                    KdEnqueueKeyboardEvent(ephyrKbd, key, FALSE);
                    break;
                }
    }
}

static Bool
ephyrCursorOffScreen(ScreenPtr *ppScreen, int *x, int *y)
{
    return FALSE;
}

static void
ephyrCrossScreen(ScreenPtr pScreen, Bool entering)
{
}

int ephyrCurScreen;             /*current event screen */

static void
ephyrWarpCursor(DeviceIntPtr pDev, ScreenPtr pScreen, int x, int y)
{
    OsBlockSIGIO();
    ephyrCurScreen = pScreen->myNum;
    miPointerWarpCursor(inputInfo.pointer, pScreen, x, y);

    OsReleaseSIGIO();
}

miPointerScreenFuncRec ephyrPointerScreenFuncs = {
    ephyrCursorOffScreen,
    ephyrCrossScreen,
    ephyrWarpCursor,
    NULL,
    NULL
};

#ifdef XF86DRI
/**
 * find if the remote window denoted by a_remote
 * is paired with an internal Window within the Xephyr server.
 * If the remove window is paired with an internal window, send an
 * expose event to the client insterested in the internal window expose event.
 *
 * Pairing happens when a drawable inside Xephyr is associated with
 * a GL surface in a DRI environment.
 * Look at the function ProcXF86DRICreateDrawable in ephyrdriext.c to
 * know a paired window is created.
 *
 * This is useful to make GL drawables (only windows for now) handle
 * expose events and send those events to clients.
 */
static void
ephyrExposePairedWindow(int a_remote)
{
    EphyrWindowPair *pair = NULL;
    RegionRec reg;
    ScreenPtr screen;

    if (!findWindowPairFromRemote(a_remote, &pair)) {
        EPHYR_LOG("did not find a pair for this window\n");
        return;
    }
    screen = pair->local->drawable.pScreen;
    RegionNull(&reg);
    RegionCopy(&reg, &pair->local->clipList);
    screen->WindowExposures(pair->local, &reg, NullRegion);
    RegionUninit(&reg);
}
#endif                          /* XF86DRI */

void
ephyrPoll(void)
{
    EphyrHostXEvent ev;

    while (hostx_get_event(&ev)) {
        switch (ev.type) {
        case EPHYR_EV_MOUSE_MOTION:
            if (!ephyrMouse ||
                !((EphyrPointerPrivate *) ephyrMouse->driverPrivate)->enabled) {
                EPHYR_LOG("skipping mouse motion:%d\n", ephyrCurScreen);
                continue;
            }
            {
                if (ev.data.mouse_motion.screen >= 0
                    && (ephyrCurScreen != ev.data.mouse_motion.screen)) {
                    EPHYR_LOG("warping mouse cursor. "
                              "cur_screen%d, motion_screen:%d\n",
                              ephyrCurScreen, ev.data.mouse_motion.screen);
                    if (ev.data.mouse_motion.screen >= 0) {
                        ephyrWarpCursor
                            (inputInfo.pointer,
                             screenInfo.screens[ev.data.mouse_motion.screen],
                             ev.data.mouse_motion.x, ev.data.mouse_motion.y);
                    }
                }
                else {
                    int x = 0, y = 0;

#ifdef XF86DRI
                    EphyrWindowPair *pair = NULL;
#endif
                    EPHYR_LOG("enqueuing mouse motion:%d\n", ephyrCurScreen);
                    x = ev.data.mouse_motion.x;
                    y = ev.data.mouse_motion.y;
                    EPHYR_LOG("initial (x,y):(%d,%d)\n", x, y);
#ifdef XF86DRI
                    EPHYR_LOG("is this window peered by a gl drawable ?\n");
                    if (findWindowPairFromRemote(ev.data.mouse_motion.window,
                                                 &pair)) {
                        EPHYR_LOG("yes, it is peered\n");
                        x += pair->local->drawable.x;
                        y += pair->local->drawable.y;
                    }
                    else {
                        EPHYR_LOG("no, it is not peered\n");
                    }
                    EPHYR_LOG("final (x,y):(%d,%d)\n", x, y);
#endif
                    KdEnqueuePointerEvent(ephyrMouse, mouseState, x, y, 0);
                }
            }
            break;

        case EPHYR_EV_MOUSE_PRESS:
            if (!ephyrMouse ||
                !((EphyrPointerPrivate *) ephyrMouse->driverPrivate)->enabled) {
                EPHYR_LOG("skipping mouse press:%d\n", ephyrCurScreen);
                continue;
            }
            EPHYR_LOG("enqueuing mouse press:%d\n", ephyrCurScreen);
            ephyrUpdateModifierState(ev.key_state);
            mouseState |= ev.data.mouse_down.button_num;
            KdEnqueuePointerEvent(ephyrMouse, mouseState | KD_MOUSE_DELTA, 0, 0,
                                  0);
            break;

        case EPHYR_EV_MOUSE_RELEASE:
            if (!ephyrMouse ||
                !((EphyrPointerPrivate *) ephyrMouse->driverPrivate)->enabled)
                continue;
            ephyrUpdateModifierState(ev.key_state);
            mouseState &= ~ev.data.mouse_up.button_num;
            EPHYR_LOG("enqueuing mouse release:%d\n", ephyrCurScreen);
            KdEnqueuePointerEvent(ephyrMouse, mouseState | KD_MOUSE_DELTA, 0, 0,
                                  0);
            break;

        case EPHYR_EV_KEY_PRESS:
            if (!ephyrKbd ||
                !((EphyrKbdPrivate *) ephyrKbd->driverPrivate)->enabled)
                continue;
            ephyrUpdateModifierState(ev.key_state);
            KdEnqueueKeyboardEvent(ephyrKbd, ev.data.key_down.scancode, FALSE);
            break;

        case EPHYR_EV_KEY_RELEASE:
            if (!ephyrKbd ||
                !((EphyrKbdPrivate *) ephyrKbd->driverPrivate)->enabled)
                continue;
            ephyrUpdateModifierState(ev.key_state);
            KdEnqueueKeyboardEvent(ephyrKbd, ev.data.key_up.scancode, TRUE);
            break;

#ifdef XF86DRI
        case EPHYR_EV_EXPOSE:
            /*
             * We only receive expose events when the expose event have
             * be generated for a drawable that is a host X window managed
             * by Xephyr. Host X windows managed by Xephyr exists for instance
             * when Xephyr is asked to create a GL drawable in a DRI environment.
             */
            ephyrExposePairedWindow(ev.data.expose.window);
            break;
#endif                          /* XF86DRI */

#ifdef RANDR
        case EPHYR_EV_CONFIGURE:
            ephyrResizeScreen(screenInfo.screens[ev.data.configure.screen],
                              ev.data.configure.width,
                              ev.data.configure.height);
            break;
#endif /* RANDR */

        default:
            break;
        }
    }
}

void
ephyrCardFini(KdCardInfo * card)
{
    EphyrPriv *priv = card->driver;

    free(priv);
}

void
ephyrGetColors(ScreenPtr pScreen, int n, xColorItem * pdefs)
{
    /* XXX Not sure if this is right */

    EPHYR_LOG("mark");

    while (n--) {
        pdefs->red = 0;
        pdefs->green = 0;
        pdefs->blue = 0;
        pdefs++;
    }

}

void
ephyrPutColors(ScreenPtr pScreen, int n, xColorItem * pdefs)
{
    int min, max, p;

    /* XXX Not sure if this is right */

    min = 256;
    max = 0;

    while (n--) {
        p = pdefs->pixel;
        if (p < min)
            min = p;
        if (p > max)
            max = p;

        hostx_set_cmap_entry(p,
                             pdefs->red >> 8,
                             pdefs->green >> 8, pdefs->blue >> 8);
        pdefs++;
    }
}

/* Mouse calls */

static Status
MouseInit(KdPointerInfo * pi)
{
    pi->driverPrivate = (EphyrPointerPrivate *)
        calloc(sizeof(EphyrPointerPrivate), 1);
    ((EphyrPointerPrivate *) pi->driverPrivate)->enabled = FALSE;
    pi->nAxes = 3;
    pi->nButtons = 32;
    free(pi->name);
    pi->name = strdup("Xephyr virtual mouse");

    /*
     * Must transform pointer coords since the pointer position
     * relative to the Xephyr window is controlled by the host server and
     * remains constant regardless of any rotation applied to the Xephyr screen.
     */
    pi->transformCoordinates = TRUE;

    ephyrMouse = pi;
    return Success;
}

static Status
MouseEnable(KdPointerInfo * pi)
{
    ((EphyrPointerPrivate *) pi->driverPrivate)->enabled = TRUE;
    return Success;
}

static void
MouseDisable(KdPointerInfo * pi)
{
    ((EphyrPointerPrivate *) pi->driverPrivate)->enabled = FALSE;
    return;
}

static void
MouseFini(KdPointerInfo * pi)
{
    ephyrMouse = NULL;
    return;
}

KdPointerDriver EphyrMouseDriver = {
    "ephyr",
    MouseInit,
    MouseEnable,
    MouseDisable,
    MouseFini,
    NULL,
};

/* Keyboard */

static Status
EphyrKeyboardInit(KdKeyboardInfo * ki)
{
    ki->driverPrivate = (EphyrKbdPrivate *)
        calloc(sizeof(EphyrKbdPrivate), 1);
    hostx_load_keymap();
    if (!ephyrKeySyms.map) {
        ErrorF("Couldn't load keymap from host\n");
        return BadAlloc;
    }
    ki->minScanCode = ephyrKeySyms.minKeyCode;
    ki->maxScanCode = ephyrKeySyms.maxKeyCode;
    free(ki->name);
    ki->name = strdup("Xephyr virtual keyboard");
    ephyrKbd = ki;
    return Success;
}

static Status
EphyrKeyboardEnable(KdKeyboardInfo * ki)
{
    ((EphyrKbdPrivate *) ki->driverPrivate)->enabled = TRUE;

    return Success;
}

static void
EphyrKeyboardDisable(KdKeyboardInfo * ki)
{
    ((EphyrKbdPrivate *) ki->driverPrivate)->enabled = FALSE;
}

static void
EphyrKeyboardFini(KdKeyboardInfo * ki)
{
    ephyrKbd = NULL;
    return;
}

static void
EphyrKeyboardLeds(KdKeyboardInfo * ki, int leds)
{
}

static void
EphyrKeyboardBell(KdKeyboardInfo * ki, int volume, int frequency, int duration)
{
}

KdKeyboardDriver EphyrKeyboardDriver = {
    "ephyr",
    EphyrKeyboardInit,
    EphyrKeyboardEnable,
    EphyrKeyboardLeds,
    EphyrKeyboardBell,
    EphyrKeyboardDisable,
    EphyrKeyboardFini,
    NULL,
};
