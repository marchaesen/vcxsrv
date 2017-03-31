/*
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Author:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Provides DPMS support and unifies all DPMS and other screen-saver
 * support in one file.  If -dpms is given on the command line, or the
 * Xdmx server is not compiled with DPMS support, then the DPMS extension
 * does not work for clients, but DPMS on the backends is still disables
 * (and restored at Xdmx server shutdown time).
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxdpms.h"
#include "dmxlog.h"
#include "dmxsync.h"
#ifdef DPMSExtension
#include "dpmsproc.h"
#endif
#include "windowstr.h"          /* For screenIsSaved */
#include <X11/extensions/dpms.h>

static int
_dmxDPMSInit(DMXScreenInfo * dmxScreen)
{
    int event_base, error_base;
    int major, minor;
    CARD16 level, standby, suspend, off;
    BOOL state;
    const char *monitor;

#ifdef DPMSExtension
    if (DPMSDisabledSwitch)
        return FALSE;
#endif

    dmxScreen->dpmsCapable = 0;

    if (!dmxScreen->beDisplay) {
        dmxLogOutput(dmxScreen,
                     "Cannot determine if DPMS supported (detached screen)\n");
        return FALSE;
    }

    if (!DPMSQueryExtension(dmxScreen->beDisplay, &event_base, &error_base)) {
        dmxLogOutput(dmxScreen, "DPMS not supported\n");
        return FALSE;
    }
    if (!DPMSGetVersion(dmxScreen->beDisplay, &major, &minor)) {
        dmxLogOutput(dmxScreen, "DPMS not supported\n");
        return FALSE;
    }
    if (!DPMSCapable(dmxScreen->beDisplay)) {
        dmxLogOutput(dmxScreen, "DPMS %d.%d (not DPMS capable)\n",
                     major, minor);
        return FALSE;
    }

    DPMSInfo(dmxScreen->beDisplay, &level, &state);
    DPMSGetTimeouts(dmxScreen->beDisplay, &standby, &suspend, &off);
    DPMSSetTimeouts(dmxScreen->beDisplay, 0, 0, 0);
    DPMSEnable(dmxScreen->beDisplay);
    DPMSForceLevel(dmxScreen->beDisplay, DPMSModeOn);
    dmxScreen->dpmsCapable = 1;
    dmxScreen->dpmsEnabled = ! !state;
    dmxScreen->dpmsStandby = standby;
    dmxScreen->dpmsSuspend = suspend;
    dmxScreen->dpmsOff = off;

    switch (level) {
    case DPMSModeOn:
        monitor = "on";
        break;
    case DPMSModeStandby:
        monitor = "standby";
        break;
    case DPMSModeSuspend:
        monitor = "suspend";
        break;
    case DPMSModeOff:
        monitor = "off";
        break;
    default:
        monitor = "unknown";
        break;
    }

    dmxLogOutput(dmxScreen,
                 "DPMS %d.%d (%s, %s, %d %d %d)\n",
                 major, minor, monitor, state ? "enabled" : "disabled",
                 standby, suspend, off);
    return TRUE;
}

/** Initialize DPMS support.  We save the current settings and turn off
 * DPMS.  The settings are restored in #dmxDPMSTerm. */
int
dmxDPMSInit(DMXScreenInfo * dmxScreen)
{
    int interval, preferBlanking, allowExposures;

    /* Turn off DPMS */
    if (!_dmxDPMSInit(dmxScreen))
        return FALSE;

    if (!dmxScreen->beDisplay)
        return FALSE;

    /* Turn off screen saver */
    XGetScreenSaver(dmxScreen->beDisplay, &dmxScreen->savedTimeout, &interval,
                    &preferBlanking, &allowExposures);
    XSetScreenSaver(dmxScreen->beDisplay, 0, interval,
                    preferBlanking, allowExposures);
    XResetScreenSaver(dmxScreen->beDisplay);
    dmxSync(dmxScreen, FALSE);
    return TRUE;
}

/** Terminate DPMS support on \a dmxScreen.  We restore the settings
 * saved in #dmxDPMSInit. */
void
dmxDPMSTerm(DMXScreenInfo * dmxScreen)
{
    int timeout, interval, preferBlanking, allowExposures;

    if (!dmxScreen->beDisplay)
        return;

    XGetScreenSaver(dmxScreen->beDisplay, &timeout, &interval,
                    &preferBlanking, &allowExposures);
    XSetScreenSaver(dmxScreen->beDisplay, dmxScreen->savedTimeout, interval,
                    preferBlanking, allowExposures);
    if (dmxScreen->dpmsCapable) {
        /* Restore saved state */
        DPMSForceLevel(dmxScreen->beDisplay, DPMSModeOn);
        DPMSSetTimeouts(dmxScreen->beDisplay, dmxScreen->dpmsStandby,
                        dmxScreen->dpmsSuspend, dmxScreen->dpmsOff);
        if (dmxScreen->dpmsEnabled)
            DPMSEnable(dmxScreen->beDisplay);
        else
            DPMSDisable(dmxScreen->beDisplay);
    }
    dmxSync(dmxScreen, FALSE);
}

/** Called when activity is detected so that DPMS power-saving mode can
 * be deactivated. */
void
dmxDPMSWakeup(void)
{
    if (screenIsSaved == SCREEN_SAVER_ON)
        dixSaveScreens(serverClient, SCREEN_SAVER_OFF, ScreenSaverReset);
#ifdef DPMSExtension
    if (DPMSPowerLevel)
        DPMSSet(serverClient, 0);
#endif
}

#ifdef DPMSExtension
void
dmxDPMSBackend(DMXScreenInfo *dmxScreen, int level)
{
    if (dmxScreen->beDisplay) {
        DPMSForceLevel(dmxScreen->beDisplay, level);
        dmxSync(dmxScreen, FALSE);
    }
}
#endif
