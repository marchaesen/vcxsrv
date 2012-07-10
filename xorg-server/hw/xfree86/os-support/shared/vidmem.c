/*
 * Copyright (c) 1993-2003 by The XFree86 Project, Inc.
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
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "input.h"
#include "scrnintstr.h"

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "xf86OSpriv.h"

/*
 * This file contains the common part of the video memory mapping functions
 */

/*
 * Get a piece of the ScrnInfoRec.  At the moment, this is only used to hold
 * the MTRR option information, but it is likely to be expanded if we do
 * auto unmapping of memory at VT switch.
 *
 */

typedef struct {
    unsigned long size;
    pointer virtBase;
    pointer mtrrInfo;
} MappingRec, *MappingPtr;

typedef struct {
    int numMappings;
    MappingPtr *mappings;
    Bool mtrrEnabled;
    MessageType mtrrFrom;
    Bool mtrrOptChecked;
    ScrnInfoPtr pScrn;
} VidMapRec, *VidMapPtr;

static int vidMapIndex = -1;

#define VIDMAPPTR(p) ((VidMapPtr)((p)->privates[vidMapIndex].ptr))

static VidMemInfo vidMemInfo = { FALSE, };
static VidMapRec vidMapRec = { 0, NULL, TRUE, X_DEFAULT, FALSE, NULL };

static VidMapPtr
getVidMapRec(int scrnIndex)
{
    VidMapPtr vp;
    ScrnInfoPtr pScrn;

    if ((scrnIndex < 0) || !(pScrn = xf86Screens[scrnIndex]))
        return &vidMapRec;

    if (vidMapIndex < 0)
        vidMapIndex = xf86AllocateScrnInfoPrivateIndex();

    if (VIDMAPPTR(pScrn) != NULL)
        return VIDMAPPTR(pScrn);

    vp = pScrn->privates[vidMapIndex].ptr = xnfcalloc(sizeof(VidMapRec), 1);
    vp->mtrrEnabled = TRUE;     /* default to enabled */
    vp->mtrrFrom = X_DEFAULT;
    vp->mtrrOptChecked = FALSE;
    vp->pScrn = pScrn;
    return vp;
}

static MappingPtr
newMapping(VidMapPtr vp)
{
    vp->mappings = xnfrealloc(vp->mappings, sizeof(MappingPtr) *
                              (vp->numMappings + 1));
    vp->mappings[vp->numMappings] = xnfcalloc(sizeof(MappingRec), 1);
    return vp->mappings[vp->numMappings++];
}

static MappingPtr
findMapping(VidMapPtr vp, pointer vbase, unsigned long size)
{
    int i;

    for (i = 0; i < vp->numMappings; i++) {
        if (vp->mappings[i]->virtBase == vbase && vp->mappings[i]->size == size)
            return vp->mappings[i];
    }
    return NULL;
}

static void
removeMapping(VidMapPtr vp, MappingPtr mp)
{
    int i, found = 0;

    for (i = 0; i < vp->numMappings; i++) {
        if (vp->mappings[i] == mp) {
            found = 1;
            free(vp->mappings[i]);
        }
        else if (found) {
            vp->mappings[i - 1] = vp->mappings[i];
        }
    }
    vp->numMappings--;
    vp->mappings[vp->numMappings] = NULL;
}

enum { OPTION_MTRR };

static const OptionInfoRec opts[] = {
    {OPTION_MTRR, "mtrr", OPTV_BOOLEAN, {0}, FALSE},
    {-1, NULL, OPTV_NONE, {0}, FALSE}
};

static void
checkMtrrOption(VidMapPtr vp)
{
    if (!vp->mtrrOptChecked && vp->pScrn && vp->pScrn->options != NULL) {
        OptionInfoPtr options;

        options = xnfalloc(sizeof(opts));
        (void) memcpy(options, opts, sizeof(opts));
        xf86ProcessOptions(vp->pScrn->scrnIndex, vp->pScrn->options, options);
        if (xf86GetOptValBool(options, OPTION_MTRR, &vp->mtrrEnabled))
            vp->mtrrFrom = X_CONFIG;
        free(options);
        vp->mtrrOptChecked = TRUE;
    }
}

void
xf86InitVidMem(void)
{
    if (!vidMemInfo.initialised) {
        memset(&vidMemInfo, 0, sizeof(VidMemInfo));
        xf86OSInitVidMem(&vidMemInfo);
    }
}

pointer
xf86MapVidMem(int ScreenNum, int Flags, unsigned long Base, unsigned long Size)
{
    pointer vbase = NULL;
    VidMapPtr vp;
    MappingPtr mp;

    if (((Flags & VIDMEM_FRAMEBUFFER) &&
         (Flags & (VIDMEM_MMIO | VIDMEM_MMIO_32BIT))))
        FatalError("Mapping memory with more than one type\n");

    xf86InitVidMem();
    if (!vidMemInfo.initialised || !vidMemInfo.mapMem)
        return NULL;

    vbase = vidMemInfo.mapMem(ScreenNum, Base, Size, Flags);

    if (!vbase || vbase == (pointer) -1)
        return NULL;

    vp = getVidMapRec(ScreenNum);
    mp = newMapping(vp);
    mp->size = Size;
    mp->virtBase = vbase;

    /*
     * Check the "mtrr" option even when MTRR isn't supported to avoid
     * warnings about unrecognised options.
     */
    checkMtrrOption(vp);

    if (vp->mtrrEnabled && vidMemInfo.setWC) {
        if (Flags & (VIDMEM_MMIO | VIDMEM_MMIO_32BIT))
            mp->mtrrInfo =
                vidMemInfo.setWC(ScreenNum, Base, Size, FALSE, vp->mtrrFrom);
        else if (Flags & VIDMEM_FRAMEBUFFER)
            mp->mtrrInfo =
                vidMemInfo.setWC(ScreenNum, Base, Size, TRUE, vp->mtrrFrom);
    }
    return vbase;
}

void
xf86UnMapVidMem(int ScreenNum, pointer Base, unsigned long Size)
{
    VidMapPtr vp;
    MappingPtr mp;

    if (!vidMemInfo.initialised || !vidMemInfo.unmapMem) {
        xf86DrvMsg(ScreenNum, X_WARNING,
                   "xf86UnMapVidMem() called before xf86MapVidMem()\n");
        return;
    }

    vp = getVidMapRec(ScreenNum);
    mp = findMapping(vp, Base, Size);
    if (!mp) {
        xf86DrvMsg(ScreenNum, X_WARNING,
                   "xf86UnMapVidMem: cannot find region for [%p,0x%lx]\n",
                   Base, Size);
        return;
    }
    if (vp->mtrrEnabled && vidMemInfo.undoWC && mp)
        vidMemInfo.undoWC(ScreenNum, mp->mtrrInfo);

    vidMemInfo.unmapMem(ScreenNum, Base, Size);
    removeMapping(vp, mp);
}

Bool
xf86CheckMTRR(int ScreenNum)
{
    VidMapPtr vp = getVidMapRec(ScreenNum);

    /*
     * Check the "mtrr" option even when MTRR isn't supported to avoid
     * warnings about unrecognised options.
     */
    checkMtrrOption(vp);

    if (vp->mtrrEnabled && vidMemInfo.setWC)
        return TRUE;

    return FALSE;
}

Bool
xf86LinearVidMem(void)
{
    xf86InitVidMem();
    return vidMemInfo.linearSupported;
}

void
xf86MapReadSideEffects(int ScreenNum, int Flags, pointer base,
                       unsigned long Size)
{
    if (!(Flags & VIDMEM_READSIDEEFFECT))
        return;

    if (!vidMemInfo.initialised || !vidMemInfo.readSideEffects)
        return;

    vidMemInfo.readSideEffects(ScreenNum, base, Size);
}
