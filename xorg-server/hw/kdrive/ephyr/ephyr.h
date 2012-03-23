/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@o-hand.com>
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

#ifndef _EPHYR_H_
#define _EPHYR_H_
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>

#include "os.h"                 /* for OsSignal() */
#include "kdrive.h"
#include "hostx.h"
#include "exa.h"

#ifdef RANDR
#include "randrstr.h"
#endif

#include "damage.h"

typedef struct _ephyrPriv {
    CARD8 *base;
    int bytes_per_line;
} EphyrPriv;

typedef struct _ephyrFakexaPriv {
    ExaDriverPtr exa;
    Bool is_synced;

    /* The following are arguments and other information from Prepare* calls
     * which are stored for use in the inner calls.
     */
    int op;
    PicturePtr pSrcPicture, pMaskPicture, pDstPicture;
    void *saved_ptrs[3];
    PixmapPtr pDst, pSrc, pMask;
    GCPtr pGC;
} EphyrFakexaPriv;

typedef struct _ephyrScrPriv {
    Rotation randr;
    Bool shadow;
    DamagePtr pDamage;
    EphyrFakexaPriv *fakexa;
} EphyrScrPriv;

extern KdCardFuncs ephyrFuncs;
extern KdKeyboardInfo *ephyrKbd;
extern KdPointerInfo *ephyrMouse;

extern miPointerScreenFuncRec ephyrPointerScreenFuncs;

Bool
 ephyrInitialize(KdCardInfo * card, EphyrPriv * priv);

Bool
 ephyrCardInit(KdCardInfo * card);

Bool
 ephyrScreenInit(KdScreenInfo * screen);

Bool
 ephyrScreenInitialize(KdScreenInfo * screen, EphyrScrPriv * scrpriv);

Bool
 ephyrInitScreen(ScreenPtr pScreen);

Bool
 ephyrFinishInitScreen(ScreenPtr pScreen);

Bool
 ephyrCreateResources(ScreenPtr pScreen);

void
 ephyrPreserve(KdCardInfo * card);

Bool
 ephyrEnable(ScreenPtr pScreen);

Bool
 ephyrDPMS(ScreenPtr pScreen, int mode);

void
 ephyrDisable(ScreenPtr pScreen);

void
 ephyrRestore(KdCardInfo * card);

void
 ephyrScreenFini(KdScreenInfo * screen);

void
 ephyrCardFini(KdCardInfo * card);

void
 ephyrGetColors(ScreenPtr pScreen, int n, xColorItem * pdefs);

void
 ephyrPutColors(ScreenPtr pScreen, int n, xColorItem * pdefs);

Bool
 ephyrMapFramebuffer(KdScreenInfo * screen);

void *ephyrWindowLinear(ScreenPtr pScreen,
                        CARD32 row,
                        CARD32 offset, int mode, CARD32 *size, void *closure);

void
 ephyrSetScreenSizes(ScreenPtr pScreen);

Bool
 ephyrUnmapFramebuffer(KdScreenInfo * screen);

void
 ephyrUnsetInternalDamage(ScreenPtr pScreen);

Bool
 ephyrSetInternalDamage(ScreenPtr pScreen);

Bool
 ephyrCreateColormap(ColormapPtr pmap);

void
 ephyrPoll(void);

#ifdef RANDR
Bool
 ephyrRandRGetInfo(ScreenPtr pScreen, Rotation * rotations);

Bool

ephyrRandRSetConfig(ScreenPtr pScreen,
                    Rotation randr, int rate, RRScreenSizePtr pSize);
Bool
 ephyrRandRInit(ScreenPtr pScreen);

void
 ephyrShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf);

#endif

void
 ephyrUpdateModifierState(unsigned int state);

extern KdPointerDriver EphyrMouseDriver;

extern KdKeyboardDriver EphyrKeyboardDriver;

extern KdOsFuncs EphyrOsFuncs;

extern Bool ephyrCursorInit(ScreenPtr pScreen);

extern void ephyrCursorEnable(ScreenPtr pScreen);

extern int ephyrBufferHeight(KdScreenInfo * screen);

/* ephyr_draw.c */

Bool
 ephyrDrawInit(ScreenPtr pScreen);

void
 ephyrDrawEnable(ScreenPtr pScreen);

void
 ephyrDrawDisable(ScreenPtr pScreen);

void
 ephyrDrawFini(ScreenPtr pScreen);

/*ephyvideo.c*/

Bool ephyrInitVideo(ScreenPtr pScreen);

#endif
