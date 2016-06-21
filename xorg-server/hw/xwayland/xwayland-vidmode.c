/*
 * Copyright (c) 1999-2003 by The XFree86 Project, Inc.
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

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include "misc.h"
#include "os.h"
#include "extinit.h"

#ifdef XF86VIDMODE
#include "xwayland.h"
#include "randrstr.h"
#include "vidmodestr.h"

static DevPrivateKeyRec xwlVidModePrivateKeyRec;
#define xwlVidModePrivateKey (&xwlVidModePrivateKeyRec)

/* Taken from xrandr, h sync frequency in KHz */
static double
mode_hsync(const xRRModeInfo *mode_info)
{
    double rate;

    if (mode_info->hTotal)
        rate = (double) mode_info->dotClock / (double) mode_info->hTotal;
    else
        rate = 0.0;

    return rate / 1000.0;
}

/* Taken from xrandr, v refresh frequency in Hz */
static double
mode_refresh(const xRRModeInfo *mode_info)
{
    double rate;
    double vTotal = mode_info->vTotal;

    if (mode_info->modeFlags & RR_DoubleScan)
	vTotal *= 2.0;

    if (mode_info->modeFlags & RR_Interlace)
	vTotal /= 2.0;

    if (mode_info->hTotal > 0.0 && vTotal > 0.0)
	rate = ((double) mode_info->dotClock /
		((double) mode_info->hTotal * (double) vTotal));
    else
        rate = 0.0;

    return rate;
}

static Bool
xwlVidModeGetCurrentModeline(ScreenPtr pScreen, DisplayModePtr *mode, int *dotClock)
{
    DisplayModePtr pMod;
    RROutputPtr output;
    RRCrtcPtr crtc;
    xRRModeInfo rrmode;

    pMod = dixLookupPrivate(&pScreen->devPrivates, xwlVidModePrivateKey);
    if (pMod == NULL)
        return FALSE;

    output = RRFirstOutput(pScreen);
    if (output == NULL)
        return FALSE;

    crtc = output->crtc;
    if (crtc == NULL)
        return FALSE;

    rrmode = crtc->mode->mode;

    pMod->next = pMod;
    pMod->prev = pMod;
    pMod->name = "";
    pMod->VScan = 1;
    pMod->Private = NULL;
    pMod->HDisplay = rrmode.width;
    pMod->HSyncStart = rrmode.hSyncStart;
    pMod->HSyncEnd = rrmode.hSyncEnd;
    pMod->HTotal = rrmode.hTotal;
    pMod->HSkew = rrmode.hSkew;
    pMod->VDisplay = rrmode.height;
    pMod->VSyncStart = rrmode.vSyncStart;
    pMod->VSyncEnd = rrmode.vSyncEnd;
    pMod->VTotal = rrmode.vTotal;
    pMod->Flags = rrmode.modeFlags;
    pMod->Clock = rrmode.dotClock / 1000.0;
    pMod->VRefresh = mode_refresh(&rrmode); /* Or RRVerticalRefresh() */
    pMod->HSync = mode_hsync(&rrmode);
    *mode = pMod;

    if (dotClock != NULL)
        *dotClock = rrmode.dotClock / 1000.0;

    return TRUE;
}

static vidMonitorValue
xwlVidModeGetMonitorValue(ScreenPtr pScreen, int valtyp, int indx)
{
    vidMonitorValue ret = { NULL, };
    DisplayModePtr pMod;

    if (!xwlVidModeGetCurrentModeline(pScreen, &pMod, NULL))
        return ret;

    switch (valtyp) {
    case VIDMODE_MON_VENDOR:
        ret.ptr = XVENDORNAME;
        break;
    case VIDMODE_MON_MODEL:
        ret.ptr = "XWAYLAND";
        break;
    case VIDMODE_MON_NHSYNC:
        ret.i = 1;
        break;
    case VIDMODE_MON_NVREFRESH:
        ret.i = 1;
        break;
    case VIDMODE_MON_HSYNC_LO:
    case VIDMODE_MON_HSYNC_HI:
        ret.f = 100.0 * pMod->HSync;
        break;
    case VIDMODE_MON_VREFRESH_LO:
    case VIDMODE_MON_VREFRESH_HI:
        ret.f = 100.0 * pMod->VRefresh;
        break;
    }
    return ret;
}

static int
xwlVidModeGetDotClock(ScreenPtr pScreen, int Clock)
{
    DisplayModePtr pMod;

    if (!xwlVidModeGetCurrentModeline(pScreen, &pMod, NULL))
        return 0;

    return pMod->Clock;

}

static int
xwlVidModeGetNumOfClocks(ScreenPtr pScreen, Bool *progClock)
{
    return 1;
}

static Bool
xwlVidModeGetClocks(ScreenPtr pScreen, int *Clocks)
{
    *Clocks = xwlVidModeGetDotClock(pScreen, 0);

    return TRUE;
}

static Bool
xwlVidModeGetNextModeline(ScreenPtr pScreen, DisplayModePtr *mode, int *dotClock)
{
    return FALSE;
}

static Bool
xwlVidModeGetFirstModeline(ScreenPtr pScreen, DisplayModePtr *mode, int *dotClock)
{
    return xwlVidModeGetCurrentModeline(pScreen, mode, dotClock);
}

static Bool
xwlVidModeDeleteModeline(ScreenPtr pScreen, DisplayModePtr mode)
{
    /* Unsupported */
    return FALSE;
}

static Bool
xwlVidModeZoomViewport(ScreenPtr pScreen, int zoom)
{
    /* Support only no zoom */
    return (zoom == 1);
}

static Bool
xwlVidModeSetViewPort(ScreenPtr pScreen, int x, int y)
{
    RROutputPtr output;
    RRCrtcPtr crtc;

    output = RRFirstOutput(pScreen);
    if (output == NULL)
        return FALSE;

    crtc = output->crtc;
    if (crtc == NULL)
        return FALSE;

    /* Support only default viewport */
    return (x == crtc->x && y == crtc->y);
}

static Bool
xwlVidModeGetViewPort(ScreenPtr pScreen, int *x, int *y)
{
    RROutputPtr output;
    RRCrtcPtr crtc;

    output = RRFirstOutput(pScreen);
    if (output == NULL)
        return FALSE;

    crtc = output->crtc;
    if (crtc == NULL)
        return FALSE;

    *x = crtc->x;
    *y = crtc->y;

    return TRUE;
}

static Bool
xwlVidModeSwitchMode(ScreenPtr pScreen, DisplayModePtr mode)
{
    /* Unsupported for now */
    return FALSE;
}

static Bool
xwlVidModeLockZoom(ScreenPtr pScreen, Bool lock)
{
    /* Unsupported for now, but pretend it works */
    return TRUE;
}

static ModeStatus
xwlVidModeCheckModeForMonitor(ScreenPtr pScreen, DisplayModePtr mode)
{
    DisplayModePtr pMod;

    /* This should not happen */
    if (!xwlVidModeGetCurrentModeline(pScreen, &pMod, NULL))
        return MODE_ERROR;

    /* Only support mode with the same HSync/VRefresh as we advertise */
    if (mode->HSync == pMod->HSync && mode->VRefresh == pMod->VRefresh)
        return MODE_OK;

    /* All the rest is unsupported - If we want to succeed, return MODE_OK instead */
    return MODE_ONE_SIZE;
}

static ModeStatus
xwlVidModeCheckModeForDriver(ScreenPtr pScreen, DisplayModePtr mode)
{
    DisplayModePtr pMod;

    /* This should not happen */
    if (!xwlVidModeGetCurrentModeline(pScreen, &pMod, NULL))
        return MODE_ERROR;

    if (mode->HTotal != pMod->HTotal)
        return MODE_BAD_HVALUE;

    if (mode->VTotal != pMod->VTotal)
        return MODE_BAD_VVALUE;

    /* Unsupported for now, but pretend it works */
    return MODE_OK;
}

static void
xwlVidModeSetCrtcForMode(ScreenPtr pScreen, DisplayModePtr mode)
{
    /* Unsupported */
    return;
}

static Bool
xwlVidModeAddModeline(ScreenPtr pScreen, DisplayModePtr mode)
{
    /* Unsupported */
    return FALSE;
}

static int
xwlVidModeGetNumOfModes(ScreenPtr pScreen)
{
    /* We have only one mode */
    return 1;
}

static Bool
xwlVidModeSetGamma(ScreenPtr pScreen, float red, float green, float blue)
{
    /* Unsupported for now, but pretend it works */
    return TRUE;
}

static Bool
xwlVidModeGetGamma(ScreenPtr pScreen, float *red, float *green, float *blue)
{
    /* Unsupported for now, but pretend it works */
    return TRUE;
}

static Bool
xwlVidModeSetGammaRamp(ScreenPtr pScreen, int size, CARD16 *r, CARD16 *g, CARD16 *b)
{
    /* Unsupported for now */
    return FALSE;
}

static Bool
xwlVidModeGetGammaRamp(ScreenPtr pScreen, int size, CARD16 *r, CARD16 *g, CARD16 *b)
{
    /* Unsupported for now */
    return FALSE;
}

static int
xwlVidModeGetGammaRampSize(ScreenPtr pScreen)
{
    /* Unsupported for now */
    return 0;
}

static Bool
xwlVidModeInit(ScreenPtr pScreen)
{
    VidModePtr pVidMode = NULL;

    pVidMode = VidModeInit(pScreen);
    if (!pVidMode)
        return FALSE;

    pVidMode->Flags = 0;
    pVidMode->Next = NULL;

    pVidMode->GetMonitorValue = xwlVidModeGetMonitorValue;
    pVidMode->GetCurrentModeline = xwlVidModeGetCurrentModeline;
    pVidMode->GetFirstModeline = xwlVidModeGetFirstModeline;
    pVidMode->GetNextModeline = xwlVidModeGetNextModeline;
    pVidMode->DeleteModeline = xwlVidModeDeleteModeline;
    pVidMode->ZoomViewport = xwlVidModeZoomViewport;
    pVidMode->GetViewPort = xwlVidModeGetViewPort;
    pVidMode->SetViewPort = xwlVidModeSetViewPort;
    pVidMode->SwitchMode = xwlVidModeSwitchMode;
    pVidMode->LockZoom = xwlVidModeLockZoom;
    pVidMode->GetNumOfClocks = xwlVidModeGetNumOfClocks;
    pVidMode->GetClocks = xwlVidModeGetClocks;
    pVidMode->CheckModeForMonitor = xwlVidModeCheckModeForMonitor;
    pVidMode->CheckModeForDriver = xwlVidModeCheckModeForDriver;
    pVidMode->SetCrtcForMode = xwlVidModeSetCrtcForMode;
    pVidMode->AddModeline = xwlVidModeAddModeline;
    pVidMode->GetDotClock = xwlVidModeGetDotClock;
    pVidMode->GetNumOfModes = xwlVidModeGetNumOfModes;
    pVidMode->SetGamma = xwlVidModeSetGamma;
    pVidMode->GetGamma = xwlVidModeGetGamma;
    pVidMode->SetGammaRamp = xwlVidModeSetGammaRamp;
    pVidMode->GetGammaRamp = xwlVidModeGetGammaRamp;
    pVidMode->GetGammaRampSize = xwlVidModeGetGammaRampSize;

    return TRUE;
}

void
xwlVidModeExtensionInit(void)
{
    int i;
    Bool enabled = FALSE;

    for (i = 0; i < screenInfo.numScreens; i++) {
        if (xwlVidModeInit (screenInfo.screens[i]))
            enabled = TRUE;
    }
    /* This means that the DDX doesn't want the vidmode extension enabled */
    if (!enabled)
        return;

    if (!dixRegisterPrivateKey(xwlVidModePrivateKey, PRIVATE_SCREEN,
                               sizeof(DisplayModeRec)))
        return;

    VidModeAddExtension(FALSE);
}

#endif                          /* XF86VIDMODE */
