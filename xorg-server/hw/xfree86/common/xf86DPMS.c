/*
 * Copyright (c) 1997-2003 by The XFree86 Project, Inc.
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

/*
 * This file contains the DPMS functions required by the extension.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "os.h"
#include "globals.h"
#include "windowstr.h"
#include "xf86.h"
#include "xf86Priv.h"
#ifdef DPMSExtension
#include <X11/extensions/dpmsconst.h>
#include "dpmsproc.h"
#endif
#ifdef XSERVER_LIBPCIACCESS
#include "xf86VGAarbiter.h"
#endif

#ifdef DPMSExtension
static DevPrivateKeyRec DPMSKeyRec;
static DevPrivateKey DPMSKey;
static Bool DPMSClose(ScreenPtr pScreen);
static int DPMSCount = 0;
#endif

Bool
xf86DPMSInit(ScreenPtr pScreen, DPMSSetProcPtr set, int flags)
{
#ifdef DPMSExtension
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    DPMSPtr pDPMS;
    void *DPMSOpt;
    MessageType enabled_from;

    DPMSKey = &DPMSKeyRec;

    if (!dixRegisterPrivateKey(&DPMSKeyRec, PRIVATE_SCREEN, sizeof(DPMSRec)))
        return FALSE;

    pDPMS = dixLookupPrivate(&pScreen->devPrivates, DPMSKey);
    pScrn->DPMSSet = set;
    pDPMS->Flags = flags;
    DPMSOpt = xf86FindOption(pScrn->options, "dpms");
    if (DPMSDisabledSwitch) {
        enabled_from = X_CMDLINE;
        DPMSEnabled = FALSE;
    }
    else if (DPMSOpt) {
        enabled_from = X_CONFIG;
        DPMSEnabled = xf86CheckBoolOption(pScrn->options, "dpms", FALSE);
        xf86MarkOptionUsed(DPMSOpt);
    }
    else {
        enabled_from = X_DEFAULT;
        DPMSEnabled = TRUE;
    }
    if (DPMSEnabled)
        xf86DrvMsg(pScreen->myNum, enabled_from, "DPMS enabled\n");
    pDPMS->Enabled = DPMSEnabled;
    pDPMS->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = DPMSClose;
    DPMSCount++;
    return TRUE;
#else
    return FALSE;
#endif
}

#ifdef DPMSExtension

static Bool
DPMSClose(ScreenPtr pScreen)
{
    DPMSPtr pDPMS;
    ScrnInfoPtr pScrn;
    /* This shouldn't happen */
    if (DPMSKey == NULL)
        return FALSE;

    pDPMS = dixLookupPrivate(&pScreen->devPrivates, DPMSKey);

    /* This shouldn't happen */
    if (!pDPMS)
        return FALSE;

    pScreen->CloseScreen = pDPMS->CloseScreen;
    pScrn = xf86ScreenToScrn(pScreen);
    /*
     * Turn on DPMS when shutting down. If this function can be used
     * depends on the order the driver wraps things. If this is called
     * after the driver has shut down everything the driver will have
     * to deal with this internally.
     */
    if (pScrn->vtSema && pScrn->DPMSSet) {
        pScrn->DPMSSet(pScrn, DPMSModeOn, 0);
    }

    if (--DPMSCount == 0)
        DPMSKey = NULL;
    return pScreen->CloseScreen(pScreen);
}

static void
DPMSSetScreen(ScrnInfoPtr pScrn, int level)
{
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    DPMSPtr pDPMS = dixLookupPrivate(&pScreen->devPrivates, DPMSKey);

    if (pDPMS && pScrn->DPMSSet && pDPMS->Enabled && pScrn->vtSema) {
        xf86VGAarbiterLock(pScrn);
        pScrn->DPMSSet(pScrn, level, 0);
        xf86VGAarbiterUnlock(pScrn);
    }
}

/*
 * DPMSSet --
 *	Device dependent DPMS mode setting hook.  This is called whenever
 *	the DPMS mode is to be changed.
 */
int
DPMSSet(ClientPtr client, int level)
{
    int rc, i;

    DPMSPowerLevel = level;

    if (DPMSKey == NULL)
        return Success;

    if (level != DPMSModeOn) {
        if (xf86IsUnblank(screenIsSaved)) {
            rc = dixSaveScreens(client, SCREEN_SAVER_FORCER, ScreenSaverActive);
            if (rc != Success)
                return rc;
        }
    } else if (!xf86IsUnblank(screenIsSaved)) {
        rc = dixSaveScreens(client, SCREEN_SAVER_OFF, ScreenSaverReset);
        if (rc != Success)
            return rc;
    }

    /* For each screen, set the DPMS level */
    for (i = 0; i < xf86NumScreens; i++) {
        DPMSSetScreen(xf86Screens[i], level);
    }
    for (i = 0; i < xf86NumGPUScreens; i++) {
        DPMSSetScreen(xf86GPUScreens[i], level);
    }
    return Success;
}

static Bool
DPMSSupportedOnScreen(ScrnInfoPtr pScrn)
{
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    DPMSPtr pDPMS = dixLookupPrivate(&pScreen->devPrivates, DPMSKey);

    return pDPMS && pScrn->DPMSSet;
}

/*
 * DPMSSupported --
 *	Return TRUE if any screen supports DPMS.
 */
Bool
DPMSSupported(void)
{
    int i;

    if (DPMSKey == NULL) {
        return FALSE;
    }

    /* For each screen, check if DPMS is supported */
    for (i = 0; i < xf86NumScreens; i++) {
        if (DPMSSupportedOnScreen(xf86Screens[i]))
            return TRUE;
    }
    for (i = 0; i < xf86NumGPUScreens; i++) {
        if (DPMSSupportedOnScreen(xf86GPUScreens[i]))
            return TRUE;
    }
    return FALSE;
}

#endif                          /* DPMSExtension */
