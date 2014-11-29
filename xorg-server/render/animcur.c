/*
 *
 * Copyright © 2002 Keith Packard, member of The XFree86 Project, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Animated cursors for X.  Not specific to Render in any way, but
 * stuck there because Render has the other cool cursor extension.
 * Besides, everyone has Render.
 *
 * Implemented as a simple layer over the core cursor code; it
 * creates composite cursors out of a set of static cursors and
 * delta times between each image.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include <X11/Xmd.h>
#include "servermd.h"
#include "scrnintstr.h"
#include "dixstruct.h"
#include "cursorstr.h"
#include "dixfontstr.h"
#include "opaque.h"
#include "picturestr.h"
#include "inputstr.h"
#include "xace.h"

typedef struct _AnimCurElt {
    CursorPtr pCursor;          /* cursor to show */
    CARD32 delay;               /* in ms */
} AnimCurElt;

typedef struct _AnimCur {
    int nelt;                   /* number of elements in the elts array */
    AnimCurElt *elts;           /* actually allocated right after the structure */
} AnimCurRec, *AnimCurPtr;

typedef struct _AnimScrPriv {
    CloseScreenProcPtr CloseScreen;

    ScreenBlockHandlerProcPtr BlockHandler;

    CursorLimitsProcPtr CursorLimits;
    DisplayCursorProcPtr DisplayCursor;
    SetCursorPositionProcPtr SetCursorPosition;
    RealizeCursorProcPtr RealizeCursor;
    UnrealizeCursorProcPtr UnrealizeCursor;
    RecolorCursorProcPtr RecolorCursor;
} AnimCurScreenRec, *AnimCurScreenPtr;

static unsigned char empty[4];

static CursorBits animCursorBits = {
    empty, empty, 2, 1, 1, 0, 0, 1
};

static DevPrivateKeyRec AnimCurScreenPrivateKeyRec;

#define AnimCurScreenPrivateKey (&AnimCurScreenPrivateKeyRec)

#define IsAnimCur(c)	    ((c) && ((c)->bits == &animCursorBits))
#define GetAnimCur(c)	    ((AnimCurPtr) ((((char *)(c) + CURSOR_REC_SIZE))))
#define GetAnimCurScreen(s) ((AnimCurScreenPtr)dixLookupPrivate(&(s)->devPrivates, AnimCurScreenPrivateKey))
#define SetAnimCurScreen(s,p) dixSetPrivate(&(s)->devPrivates, AnimCurScreenPrivateKey, p)

#define Wrap(as,s,elt,func) (((as)->elt = (s)->elt), (s)->elt = func)
#define Unwrap(as,s,elt)    ((s)->elt = (as)->elt)

static Bool
AnimCurCloseScreen(ScreenPtr pScreen)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);
    Bool ret;

    Unwrap(as, pScreen, CloseScreen);

    Unwrap(as, pScreen, CursorLimits);
    Unwrap(as, pScreen, DisplayCursor);
    Unwrap(as, pScreen, SetCursorPosition);
    Unwrap(as, pScreen, RealizeCursor);
    Unwrap(as, pScreen, UnrealizeCursor);
    Unwrap(as, pScreen, RecolorCursor);
    SetAnimCurScreen(pScreen, 0);
    ret = (*pScreen->CloseScreen) (pScreen);
    free(as);
    return ret;
}

static void
AnimCurCursorLimits(DeviceIntPtr pDev,
                    ScreenPtr pScreen,
                    CursorPtr pCursor, BoxPtr pHotBox, BoxPtr pTopLeftBox)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);

    Unwrap(as, pScreen, CursorLimits);
    if (IsAnimCur(pCursor)) {
        AnimCurPtr ac = GetAnimCur(pCursor);

        (*pScreen->CursorLimits) (pDev, pScreen, ac->elts[0].pCursor,
                                  pHotBox, pTopLeftBox);
    }
    else {
        (*pScreen->CursorLimits) (pDev, pScreen, pCursor, pHotBox, pTopLeftBox);
    }
    Wrap(as, pScreen, CursorLimits, AnimCurCursorLimits);
}

/*
 * This has to be a screen block handler instead of a generic
 * block handler so that it is well ordered with respect to the DRI
 * block handler responsible for releasing the hardware to DRI clients
 */

static void
AnimCurScreenBlockHandler(ScreenPtr pScreen,
                          void *pTimeout, void *pReadmask)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);
    DeviceIntPtr dev;
    Bool activeDevice = FALSE;
    CARD32 now = 0, soonest = ~0;       /* earliest time to wakeup again */

    Unwrap(as, pScreen, BlockHandler);

    for (dev = inputInfo.devices; dev; dev = dev->next) {
        if (IsPointerDevice(dev) && pScreen == dev->spriteInfo->anim.pScreen) {
            if (!activeDevice) {
                now = GetTimeInMillis();
                activeDevice = TRUE;
            }

            if ((INT32) (now - dev->spriteInfo->anim.time) >= 0) {
                AnimCurPtr ac = GetAnimCur(dev->spriteInfo->anim.pCursor);
                int elt = (dev->spriteInfo->anim.elt + 1) % ac->nelt;
                DisplayCursorProcPtr DisplayCursor;

                /*
                 * Not a simple Unwrap/Wrap as this
                 * isn't called along the DisplayCursor
                 * wrapper chain.
                 */
                DisplayCursor = pScreen->DisplayCursor;
                pScreen->DisplayCursor = as->DisplayCursor;
                (void) (*pScreen->DisplayCursor) (dev,
                                                  pScreen,
                                                  ac->elts[elt].pCursor);
                as->DisplayCursor = pScreen->DisplayCursor;
                pScreen->DisplayCursor = DisplayCursor;

                dev->spriteInfo->anim.elt = elt;
                dev->spriteInfo->anim.time = now + ac->elts[elt].delay;
            }

            if (soonest > dev->spriteInfo->anim.time)
                soonest = dev->spriteInfo->anim.time;
        }
    }

    if (activeDevice)
        AdjustWaitForDelay(pTimeout, soonest - now);

    (*pScreen->BlockHandler) (pScreen, pTimeout, pReadmask);
    if (activeDevice)
        Wrap(as, pScreen, BlockHandler, AnimCurScreenBlockHandler);
    else
        as->BlockHandler = NULL;
}

static Bool
AnimCurDisplayCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);
    Bool ret;

    if (IsFloating(pDev))
        return FALSE;

    Unwrap(as, pScreen, DisplayCursor);
    if (IsAnimCur(pCursor)) {
        if (pCursor != pDev->spriteInfo->anim.pCursor) {
            AnimCurPtr ac = GetAnimCur(pCursor);

            ret = (*pScreen->DisplayCursor)
                (pDev, pScreen, ac->elts[0].pCursor);
            if (ret) {
                pDev->spriteInfo->anim.elt = 0;
                pDev->spriteInfo->anim.time =
                    GetTimeInMillis() + ac->elts[0].delay;
                pDev->spriteInfo->anim.pCursor = pCursor;
                pDev->spriteInfo->anim.pScreen = pScreen;

                if (!as->BlockHandler)
                    Wrap(as, pScreen, BlockHandler, AnimCurScreenBlockHandler);
            }
        }
        else
            ret = TRUE;
    }
    else {
        pDev->spriteInfo->anim.pCursor = 0;
        pDev->spriteInfo->anim.pScreen = 0;
        ret = (*pScreen->DisplayCursor) (pDev, pScreen, pCursor);
    }
    Wrap(as, pScreen, DisplayCursor, AnimCurDisplayCursor);
    return ret;
}

static Bool
AnimCurSetCursorPosition(DeviceIntPtr pDev,
                         ScreenPtr pScreen, int x, int y, Bool generateEvent)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);
    Bool ret;

    Unwrap(as, pScreen, SetCursorPosition);
    if (pDev->spriteInfo->anim.pCursor) {
        pDev->spriteInfo->anim.pScreen = pScreen;

        if (!as->BlockHandler)
            Wrap(as, pScreen, BlockHandler, AnimCurScreenBlockHandler);
    }
    ret = (*pScreen->SetCursorPosition) (pDev, pScreen, x, y, generateEvent);
    Wrap(as, pScreen, SetCursorPosition, AnimCurSetCursorPosition);
    return ret;
}

static Bool
AnimCurRealizeCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);
    Bool ret;

    Unwrap(as, pScreen, RealizeCursor);
    if (IsAnimCur(pCursor))
        ret = TRUE;
    else
        ret = (*pScreen->RealizeCursor) (pDev, pScreen, pCursor);
    Wrap(as, pScreen, RealizeCursor, AnimCurRealizeCursor);
    return ret;
}

static Bool
AnimCurUnrealizeCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);
    Bool ret;

    Unwrap(as, pScreen, UnrealizeCursor);
    if (IsAnimCur(pCursor)) {
        AnimCurPtr ac = GetAnimCur(pCursor);
        int i;

        if (pScreen->myNum == 0)
            for (i = 0; i < ac->nelt; i++)
                FreeCursor(ac->elts[i].pCursor, 0);
        ret = TRUE;
    }
    else
        ret = (*pScreen->UnrealizeCursor) (pDev, pScreen, pCursor);
    Wrap(as, pScreen, UnrealizeCursor, AnimCurUnrealizeCursor);
    return ret;
}

static void
AnimCurRecolorCursor(DeviceIntPtr pDev,
                     ScreenPtr pScreen, CursorPtr pCursor, Bool displayed)
{
    AnimCurScreenPtr as = GetAnimCurScreen(pScreen);

    Unwrap(as, pScreen, RecolorCursor);
    if (IsAnimCur(pCursor)) {
        AnimCurPtr ac = GetAnimCur(pCursor);
        int i;

        for (i = 0; i < ac->nelt; i++)
            (*pScreen->RecolorCursor) (pDev, pScreen, ac->elts[i].pCursor,
                                       displayed &&
                                       pDev->spriteInfo->anim.elt == i);
    }
    else
        (*pScreen->RecolorCursor) (pDev, pScreen, pCursor, displayed);
    Wrap(as, pScreen, RecolorCursor, AnimCurRecolorCursor);
}

Bool
AnimCurInit(ScreenPtr pScreen)
{
    AnimCurScreenPtr as;

    if (!dixRegisterPrivateKey(&AnimCurScreenPrivateKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;

    as = (AnimCurScreenPtr) malloc(sizeof(AnimCurScreenRec));
    if (!as)
        return FALSE;
    Wrap(as, pScreen, CloseScreen, AnimCurCloseScreen);

    as->BlockHandler = NULL;

    Wrap(as, pScreen, CursorLimits, AnimCurCursorLimits);
    Wrap(as, pScreen, DisplayCursor, AnimCurDisplayCursor);
    Wrap(as, pScreen, SetCursorPosition, AnimCurSetCursorPosition);
    Wrap(as, pScreen, RealizeCursor, AnimCurRealizeCursor);
    Wrap(as, pScreen, UnrealizeCursor, AnimCurUnrealizeCursor);
    Wrap(as, pScreen, RecolorCursor, AnimCurRecolorCursor);
    SetAnimCurScreen(pScreen, as);
    return TRUE;
}

int
AnimCursorCreate(CursorPtr *cursors, CARD32 *deltas, int ncursor,
                 CursorPtr *ppCursor, ClientPtr client, XID cid)
{
    CursorPtr pCursor;
    int rc, i;
    AnimCurPtr ac;

    for (i = 0; i < screenInfo.numScreens; i++)
        if (!GetAnimCurScreen(screenInfo.screens[i]))
            return BadImplementation;

    for (i = 0; i < ncursor; i++)
        if (IsAnimCur(cursors[i]))
            return BadMatch;

    pCursor = (CursorPtr) calloc(CURSOR_REC_SIZE +
                                 sizeof(AnimCurRec) +
                                 ncursor * sizeof(AnimCurElt), 1);
    if (!pCursor)
        return BadAlloc;
    dixInitPrivates(pCursor, pCursor + 1, PRIVATE_CURSOR);
    pCursor->bits = &animCursorBits;
    pCursor->refcnt = 1;

    pCursor->foreRed = cursors[0]->foreRed;
    pCursor->foreGreen = cursors[0]->foreGreen;
    pCursor->foreBlue = cursors[0]->foreBlue;

    pCursor->backRed = cursors[0]->backRed;
    pCursor->backGreen = cursors[0]->backGreen;
    pCursor->backBlue = cursors[0]->backBlue;

    pCursor->id = cid;

    /* security creation/labeling check */
    rc = XaceHook(XACE_RESOURCE_ACCESS, client, cid, RT_CURSOR, pCursor,
                  RT_NONE, NULL, DixCreateAccess);
    if (rc != Success) {
        dixFiniPrivates(pCursor, PRIVATE_CURSOR);
        free(pCursor);
        return rc;
    }

    /*
     * Fill in the AnimCurRec
     */
    animCursorBits.refcnt++;
    ac = GetAnimCur(pCursor);
    ac->nelt = ncursor;
    ac->elts = (AnimCurElt *) (ac + 1);

    for (i = 0; i < ncursor; i++) {
        ac->elts[i].pCursor = RefCursor(cursors[i]);
        ac->elts[i].delay = deltas[i];
    }

    *ppCursor = pCursor;
    return Success;
}
