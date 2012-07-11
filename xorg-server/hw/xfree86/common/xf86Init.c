/*
 * Loosely based on code bearing the following copyright:
 *
 *   Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 */
/*
 * Copyright (c) 1992-2003 by The XFree86 Project, Inc.
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

#include <stdlib.h>
#include <errno.h>

#undef HAS_UTSNAME
#if !defined(WIN32)
#define HAS_UTSNAME 1
#include <sys/utsname.h>
#endif

#include <X11/X.h>
#include <X11/Xmd.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include "input.h"
#include "servermd.h"
#include "windowstr.h"
#include "scrnintstr.h"
#include "site.h"
#include "mi.h"

#include "compiler.h"

#include "loaderProcs.h"
#ifdef XFreeXDGA
#include "dgaproc.h"
#endif

#define XF86_OS_PRIVS
#include "xf86.h"
#include "xf86Priv.h"
#include "xf86Config.h"
#include "xf86_OSlib.h"
#include "xf86cmap.h"
#include "xorgVersion.h"
#include "xf86Build.h"
#include "mipointer.h"
#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>
#include "xf86DDC.h"
#include "xf86Xinput.h"
#include "xf86InPriv.h"
#include "picturestr.h"

#include "xf86Bus.h"
#ifdef XSERVER_LIBPCIACCESS
#include "xf86VGAarbiter.h"
#endif
#include "globals.h"
#include "xserver-properties.h"

#ifdef DPMSExtension
#include <X11/extensions/dpmsconst.h>
#include "dpmsproc.h"
#endif
#include <hotplug.h>

#ifdef XF86PM
void (*xf86OSPMClose) (void) = NULL;
#endif
static Bool xorgHWOpenConsole = FALSE;

/* Common pixmap formats */

static PixmapFormatRec formats[MAXFORMATS] = {
    {1, 1, BITMAP_SCANLINE_PAD},
    {4, 8, BITMAP_SCANLINE_PAD},
    {8, 8, BITMAP_SCANLINE_PAD},
    {15, 16, BITMAP_SCANLINE_PAD},
    {16, 16, BITMAP_SCANLINE_PAD},
    {24, 32, BITMAP_SCANLINE_PAD},
    {32, 32, BITMAP_SCANLINE_PAD},
};

static int numFormats = 7;
static Bool formatsDone = FALSE;

#ifndef OSNAME
#define OSNAME " unknown"
#endif
#ifndef OSVENDOR
#define OSVENDOR ""
#endif
#ifndef PRE_RELEASE
#define PRE_RELEASE XORG_VERSION_SNAP
#endif

static void
xf86PrintBanner(void)
{
#if PRE_RELEASE
    xf86ErrorFVerb(0, "\n"
                   "This is a pre-release version of the X server from "
                   XVENDORNAME ".\n" "It is not supported in any way.\n"
                   "Bugs may be filed in the bugzilla at http://bugs.freedesktop.org/.\n"
                   "Select the \"xorg\" product for bugs you find in this release.\n"
                   "Before reporting bugs in pre-release versions please check the\n"
                   "latest version in the X.Org Foundation git repository.\n"
                   "See http://wiki.x.org/wiki/GitPage for git access instructions.\n");
#endif
    xf86ErrorFVerb(0, "\nX.Org X Server %d.%d.%d",
                   XORG_VERSION_MAJOR, XORG_VERSION_MINOR, XORG_VERSION_PATCH);
#if XORG_VERSION_SNAP > 0
    xf86ErrorFVerb(0, ".%d", XORG_VERSION_SNAP);
#endif

#if XORG_VERSION_SNAP >= 900
    /* When the minor number is 99, that signifies that the we are making
     * a release candidate for a major version.  (X.0.0)
     * When the patch number is 99, that signifies that the we are making
     * a release candidate for a minor version.  (X.Y.0)
     * When the patch number is < 99, then we are making a release
     * candidate for the next point release.  (X.Y.Z)
     */
#if XORG_VERSION_MINOR >= 99
    xf86ErrorFVerb(0, " (%d.0.0 RC %d)", XORG_VERSION_MAJOR + 1,
                   XORG_VERSION_SNAP - 900);
#elif XORG_VERSION_PATCH == 99
    xf86ErrorFVerb(0, " (%d.%d.0 RC %d)", XORG_VERSION_MAJOR,
                   XORG_VERSION_MINOR + 1, XORG_VERSION_SNAP - 900);
#else
    xf86ErrorFVerb(0, " (%d.%d.%d RC %d)", XORG_VERSION_MAJOR,
                   XORG_VERSION_MINOR, XORG_VERSION_PATCH + 1,
                   XORG_VERSION_SNAP - 900);
#endif
#endif

#ifdef XORG_CUSTOM_VERSION
    xf86ErrorFVerb(0, " (%s)", XORG_CUSTOM_VERSION);
#endif
#ifndef XORG_DATE
#define XORG_DATE "Unknown"
#endif
    xf86ErrorFVerb(0, "\nRelease Date: %s\n", XORG_DATE);
    xf86ErrorFVerb(0, "X Protocol Version %d, Revision %d\n",
                   X_PROTOCOL, X_PROTOCOL_REVISION);
    xf86ErrorFVerb(0, "Build Operating System: %s %s\n", OSNAME, OSVENDOR);
#ifdef HAS_UTSNAME
    {
        struct utsname name;

        /* Linux & BSD state that 0 is success, SysV (including Solaris, HP-UX,
           and Irix) and Single Unix Spec 3 just say that non-negative is success.
           All agree that failure is represented by a negative number.
         */
        if (uname(&name) >= 0) {
            xf86ErrorFVerb(0, "Current Operating System: %s %s %s %s %s\n",
                           name.sysname, name.nodename, name.release,
                           name.version, name.machine);
#ifdef linux
            do {
                char buf[80];
                int fd = open("/proc/cmdline", O_RDONLY);

                if (fd != -1) {
                    xf86ErrorFVerb(0, "Kernel command line: ");
                    memset(buf, 0, 80);
                    while (read(fd, buf, 80) > 0) {
                        xf86ErrorFVerb(0, "%.80s", buf);
                        memset(buf, 0, 80);
                    }
                    close(fd);
                }
            } while (0);
#endif
        }
    }
#endif
#if defined(BUILD_DATE) && (BUILD_DATE > 19000000)
    {
        struct tm t;
        char buf[100];

        memset(&t, 0, sizeof(t));
        memset(buf, 0, sizeof(buf));
        t.tm_mday = BUILD_DATE % 100;
        t.tm_mon = (BUILD_DATE / 100) % 100 - 1;
        t.tm_year = BUILD_DATE / 10000 - 1900;
#if defined(BUILD_TIME)
        t.tm_sec = BUILD_TIME % 100;
        t.tm_min = (BUILD_TIME / 100) % 100;
        t.tm_hour = (BUILD_TIME / 10000) % 100;
        if (strftime(buf, sizeof(buf), "%d %B %Y  %I:%M:%S%p", &t))
            xf86ErrorFVerb(0, "Build Date: %s\n", buf);
#else
        if (strftime(buf, sizeof(buf), "%d %B %Y", &t))
            xf86ErrorFVerb(0, "Build Date: %s\n", buf);
#endif
    }
#endif
#if defined(BUILDERSTRING)
    xf86ErrorFVerb(0, "%s \n", BUILDERSTRING);
#endif
    xf86ErrorFVerb(0, "Current version of pixman: %s\n",
                   pixman_version_string());
    xf86ErrorFVerb(0, "\tBefore reporting problems, check "
                   "" __VENDORDWEBSUPPORT__ "\n"
                   "\tto make sure that you have the latest version.\n");
}

static void
xf86PrintMarkers(void)
{
    LogPrintMarkers();
}

Bool
xf86PrivsElevated(void)
{
    static Bool privsTested = FALSE;
    static Bool privsElevated = TRUE;

    if (!privsTested) {
#if defined(WIN32)
        privsElevated = FALSE;
#else
        if ((getuid() != geteuid()) || (getgid() != getegid())) {
            privsElevated = TRUE;
        }
        else {
#if defined(HAVE_ISSETUGID)
            privsElevated = issetugid();
#elif defined(HAVE_GETRESUID)
            uid_t ruid, euid, suid;
            gid_t rgid, egid, sgid;

            if ((getresuid(&ruid, &euid, &suid) == 0) &&
                (getresgid(&rgid, &egid, &sgid) == 0)) {
                privsElevated = (euid != suid) || (egid != sgid);
            }
            else {
                printf("Failed getresuid or getresgid");
                /* Something went wrong, make defensive assumption */
                privsElevated = TRUE;
            }
#else
            if (getuid() == 0) {
                /* running as root: uid==euid==0 */
                privsElevated = FALSE;
            }
            else {
                /*
                 * If there are saved ID's the process might still be privileged
                 * even though the above test succeeded. If issetugid() and
                 * getresgid() aren't available, test this by trying to set
                 * euid to 0.
                 */
                unsigned int oldeuid;

                oldeuid = geteuid();

                if (seteuid(0) != 0) {
                    privsElevated = FALSE;
                }
                else {
                    if (seteuid(oldeuid) != 0) {
                        FatalError("Failed to drop privileges.  Exiting\n");
                    }
                    privsElevated = TRUE;
                }
            }
#endif
        }
#endif
        privsTested = TRUE;
    }
    return privsElevated;
}

static Bool
xf86CreateRootWindow(WindowPtr pWin)
{
    int ret = TRUE;
    int err = Success;
    ScreenPtr pScreen = pWin->drawable.pScreen;
    RootWinPropPtr pProp;
    CreateWindowProcPtr CreateWindow = (CreateWindowProcPtr)
        dixLookupPrivate(&pScreen->devPrivates, xf86CreateRootWindowKey);

    DebugF("xf86CreateRootWindow(%p)\n", pWin);

    if (pScreen->CreateWindow != xf86CreateRootWindow) {
        /* Can't find hook we are hung on */
        xf86DrvMsg(pScreen->myNum, X_WARNING /* X_ERROR */ ,
                   "xf86CreateRootWindow %p called when not in pScreen->CreateWindow %p n",
                   (void *) xf86CreateRootWindow,
                   (void *) pScreen->CreateWindow);
    }

    /* Unhook this function ... */
    pScreen->CreateWindow = CreateWindow;
    dixSetPrivate(&pScreen->devPrivates, xf86CreateRootWindowKey, NULL);

    /* ... and call the previous CreateWindow fuction, if any */
    if (NULL != pScreen->CreateWindow) {
        ret = (*pScreen->CreateWindow) (pWin);
    }

    /* Now do our stuff */
    if (xf86RegisteredPropertiesTable != NULL) {
        if (pWin->parent == NULL && xf86RegisteredPropertiesTable != NULL) {
            for (pProp = xf86RegisteredPropertiesTable[pScreen->myNum];
                 pProp != NULL && err == Success; pProp = pProp->next) {
                Atom prop;

                prop = MakeAtom(pProp->name, strlen(pProp->name), TRUE);
                err = dixChangeWindowProperty(serverClient, pWin,
                                              prop, pProp->type,
                                              pProp->format, PropModeReplace,
                                              pProp->size, pProp->data, FALSE);
            }

            /* Look at err */
            ret &= (err == Success);

        }
        else {
            xf86Msg(X_ERROR, "xf86CreateRootWindow unexpectedly called with "
                    "non-root window %p (parent %p)\n",
                    (void *) pWin, (void *) pWin->parent);
            ret = FALSE;
        }
    }

    DebugF("xf86CreateRootWindow() returns %d\n", ret);
    return ret;
}

static void
InstallSignalHandlers(void)
{
    /*
     * Install signal handler for unexpected signals
     */
    xf86Info.caughtSignal = FALSE;
    if (!xf86Info.notrapSignals) {
        OsRegisterSigWrapper(xf86SigWrapper);
    }
    else {
        signal(SIGSEGV, SIG_DFL);
        signal(SIGILL, SIG_DFL);
#ifdef SIGEMT
        signal(SIGEMT, SIG_DFL);
#endif
        signal(SIGFPE, SIG_DFL);
        signal(SIGBUS, SIG_DFL);
        signal(SIGSYS, SIG_DFL);
        signal(SIGXCPU, SIG_DFL);
        signal(SIGXFSZ, SIG_DFL);
    }
}

/*
 * InitOutput --
 *	Initialize screenInfo for all actually accessible framebuffers.
 *      That includes vt-manager setup, querying all possible devices and
 *      collecting the pixmap formats.
 */
void
InitOutput(ScreenInfo * pScreenInfo, int argc, char **argv)
{
    int i, j, k, scr_index;
    char **modulelist;
    pointer *optionlist;
    Pix24Flags screenpix24, pix24;
    MessageType pix24From = X_DEFAULT;
    Bool pix24Fail = FALSE;
    Bool autoconfig = FALSE;
    Bool sigio_blocked = FALSE;
    GDevPtr configured_device;

    xf86Initialising = TRUE;

    config_pre_init();

    if (serverGeneration == 1) {
        if ((xf86ServerName = strrchr(argv[0], '/')) != 0)
            xf86ServerName++;
        else
            xf86ServerName = argv[0];

        xf86PrintBanner();
        xf86PrintMarkers();
        if (xf86LogFile) {
            time_t t;
            const char *ct;

            t = time(NULL);
            ct = ctime(&t);
            xf86MsgVerb(xf86LogFileFrom, 0, "Log file: \"%s\", Time: %s",
                        xf86LogFile, ct);
        }

        /* Read and parse the config file */
        if (!xf86DoConfigure && !xf86DoShowOptions) {
            switch (xf86HandleConfigFile(FALSE)) {
            case CONFIG_OK:
                break;
            case CONFIG_PARSE_ERROR:
                xf86Msg(X_ERROR, "Error parsing the config file\n");
                return;
            case CONFIG_NOFILE:
                autoconfig = TRUE;
                break;
            }
        }

        InstallSignalHandlers();

        /* Initialise the loader */
        LoaderInit();

        /* Tell the loader the default module search path */
        LoaderSetPath(xf86ModulePath);

        if (xf86Info.ignoreABI) {
            LoaderSetOptions(LDR_OPT_ABI_MISMATCH_NONFATAL);
        }

        if (xf86DoShowOptions)
            DoShowOptions();

        /* Do a general bus probe.  This will be a PCI probe for x86 platforms */
        xf86BusProbe();

        if (xf86DoConfigure)
            DoConfigure();

        if (autoconfig) {
            if (!xf86AutoConfig()) {
                xf86Msg(X_ERROR, "Auto configuration failed\n");
                return;
            }
        }

#ifdef XF86PM
        xf86OSPMClose = xf86OSPMOpen();
#endif

        xf86ExtensionInit();

        /* Load all modules specified explicitly in the config file */
        if ((modulelist = xf86ModulelistFromConfig(&optionlist))) {
            xf86LoadModules(modulelist, optionlist);
            free(modulelist);
            free(optionlist);
        }

        /* Load all driver modules specified in the config file */
        /* If there aren't any specified in the config file, autoconfig them */
        /* FIXME: Does not handle multiple active screen sections, but I'm not
         * sure if we really want to handle that case*/
        configured_device = xf86ConfigLayout.screens->screen->device;
        if ((!configured_device) || (!configured_device->driver)) {
            if (!autoConfigDevice(configured_device)) {
                xf86Msg(X_ERROR, "Automatic driver configuration failed\n");
                return;
            }
        }
        if ((modulelist = xf86DriverlistFromConfig())) {
            xf86LoadModules(modulelist, NULL);
            free(modulelist);
        }

        /* Load all input driver modules specified in the config file. */
        if ((modulelist = xf86InputDriverlistFromConfig())) {
            xf86LoadModules(modulelist, NULL);
            free(modulelist);
        }

        /*
         * It is expected that xf86AddDriver()/xf86AddInputDriver will be
         * called for each driver as it is loaded.  Those functions save the
         * module pointers for drivers.
         * XXX Nothing keeps track of them for other modules.
         */
        /* XXX What do we do if not all of these could be loaded? */

        /*
         * At this point, xf86DriverList[] is all filled in with entries for
         * each of the drivers to try and xf86NumDrivers has the number of
         * drivers.  If there are none, return now.
         */

        if (xf86NumDrivers == 0) {
            xf86Msg(X_ERROR, "No drivers available.\n");
            return;
        }

        /*
         * Call each of the Identify functions and call the driverFunc to check
         * if HW access is required.  The Identify functions print out some
         * identifying information, and anything else that might be
         * needed at this early stage.
         */

        for (i = 0; i < xf86NumDrivers; i++) {
            if (xf86DriverList[i]->Identify != NULL)
                xf86DriverList[i]->Identify(0);

            if (!xorgHWAccess || !xorgHWOpenConsole) {
                xorgHWFlags flags;

                if (!xf86DriverList[i]->driverFunc
                    || !xf86DriverList[i]->driverFunc(NULL,
                                                      GET_REQUIRED_HW_INTERFACES,
                                                      &flags))
                    flags = HW_IO;

                if (NEED_IO_ENABLED(flags))
                    xorgHWAccess = TRUE;
                if (!(flags & HW_SKIP_CONSOLE))
                    xorgHWOpenConsole = TRUE;
            }
        }

        if (xorgHWOpenConsole)
            xf86OpenConsole();
        else
            xf86Info.dontVTSwitch = TRUE;

        if (xf86BusConfig() == FALSE)
            return;

        xf86PostProbe();

        /*
         * Sort the drivers to match the requested ording.  Using a slow
         * bubble sort.
         */
        for (j = 0; j < xf86NumScreens - 1; j++) {
            for (i = 0; i < xf86NumScreens - j - 1; i++) {
                if (xf86Screens[i + 1]->confScreen->screennum <
                    xf86Screens[i]->confScreen->screennum) {
                    ScrnInfoPtr tmpScrn = xf86Screens[i + 1];

                    xf86Screens[i + 1] = xf86Screens[i];
                    xf86Screens[i] = tmpScrn;
                }
            }
        }
        /* Fix up the indexes */
        for (i = 0; i < xf86NumScreens; i++) {
            xf86Screens[i]->scrnIndex = i;
        }

        /*
         * Call the driver's PreInit()'s to complete initialisation for the first
         * generation.
         */

        for (i = 0; i < xf86NumScreens; i++) {
            xf86VGAarbiterScrnInit(xf86Screens[i]);
            xf86VGAarbiterLock(xf86Screens[i]);
            if (xf86Screens[i]->PreInit &&
                xf86Screens[i]->PreInit(xf86Screens[i], 0))
                xf86Screens[i]->configured = TRUE;
            xf86VGAarbiterUnlock(xf86Screens[i]);
        }
        for (i = 0; i < xf86NumScreens; i++)
            if (!xf86Screens[i]->configured)
                xf86DeleteScreen(xf86Screens[i--]);

        for (i = 0; i < xf86NumGPUScreens; i++) {
            xf86VGAarbiterScrnInit(xf86GPUScreens[i]);
            xf86VGAarbiterLock(xf86GPUScreens[i]);
            if (xf86GPUScreens[i]->PreInit &&
                xf86GPUScreens[i]->PreInit(xf86GPUScreens[i], 0))
                xf86GPUScreens[i]->configured = TRUE;
            xf86VGAarbiterUnlock(xf86GPUScreens[i]);
        }
        for (i = 0; i < xf86NumGPUScreens; i++)
            if (!xf86GPUScreens[i]->configured)
                xf86DeleteScreen(xf86GPUScreens[i--]);

        /*
         * If no screens left, return now.
         */

        if (xf86NumScreens == 0) {
            xf86Msg(X_ERROR,
                    "Screen(s) found, but none have a usable configuration.\n");
            return;
        }

        for (i = 0; i < xf86NumScreens; i++) {
            if (xf86Screens[i]->name == NULL) {
                XNFasprintf(&xf86Screens[i]->name, "screen%d", i);
                xf86MsgVerb(X_WARNING, 0,
                            "Screen driver %d has no name set, using `%s'.\n",
                            i, xf86Screens[i]->name);
            }
        }

        /* Remove (unload) drivers that are not required */
        for (i = 0; i < xf86NumDrivers; i++)
            if (xf86DriverList[i] && xf86DriverList[i]->refCount <= 0)
                xf86DeleteDriver(i);

        /*
         * At this stage we know how many screens there are.
         */

        for (i = 0; i < xf86NumScreens; i++)
            xf86InitViewport(xf86Screens[i]);

        /*
         * Collect all pixmap formats and check for conflicts at the display
         * level.  Should we die here?  Or just delete the offending screens?
         */
        screenpix24 = Pix24DontCare;
        for (i = 0; i < xf86NumScreens; i++) {
            if (xf86Screens[i]->imageByteOrder !=
                xf86Screens[0]->imageByteOrder)
                FatalError("Inconsistent display bitmapBitOrder.  Exiting\n");
            if (xf86Screens[i]->bitmapScanlinePad !=
                xf86Screens[0]->bitmapScanlinePad)
                FatalError
                    ("Inconsistent display bitmapScanlinePad.  Exiting\n");
            if (xf86Screens[i]->bitmapScanlineUnit !=
                xf86Screens[0]->bitmapScanlineUnit)
                FatalError
                    ("Inconsistent display bitmapScanlineUnit.  Exiting\n");
            if (xf86Screens[i]->bitmapBitOrder !=
                xf86Screens[0]->bitmapBitOrder)
                FatalError("Inconsistent display bitmapBitOrder.  Exiting\n");

            /* Determine the depth 24 pixmap format the screens would like */
            if (xf86Screens[i]->pixmap24 != Pix24DontCare) {
                if (screenpix24 == Pix24DontCare)
                    screenpix24 = xf86Screens[i]->pixmap24;
                else if (screenpix24 != xf86Screens[i]->pixmap24)
                    FatalError
                        ("Inconsistent depth 24 pixmap format.  Exiting\n");
            }
        }
        /* check if screenpix24 is consistent with the config/cmdline */
        if (xf86Info.pixmap24 != Pix24DontCare) {
            pix24 = xf86Info.pixmap24;
            pix24From = xf86Info.pix24From;
            if (screenpix24 != Pix24DontCare &&
                screenpix24 != xf86Info.pixmap24)
                pix24Fail = TRUE;
        }
        else if (screenpix24 != Pix24DontCare) {
            pix24 = screenpix24;
            pix24From = X_PROBED;
        }
        else
            pix24 = Pix24Use32;

        if (pix24Fail)
            FatalError("Screen(s) can't use the required depth 24 pixmap format"
                       " (%d).  Exiting\n", PIX24TOBPP(pix24));

        /* Initialise the depth 24 format */
        for (j = 0; j < numFormats && formats[j].depth != 24; j++);
        formats[j].bitsPerPixel = PIX24TOBPP(pix24);

        /* Collect additional formats */
        for (i = 0; i < xf86NumScreens; i++) {
            for (j = 0; j < xf86Screens[i]->numFormats; j++) {
                for (k = 0;; k++) {
                    if (k >= numFormats) {
                        if (k >= MAXFORMATS)
                            FatalError("Too many pixmap formats!  Exiting\n");
                        formats[k] = xf86Screens[i]->formats[j];
                        numFormats++;
                        break;
                    }
                    if (formats[k].depth == xf86Screens[i]->formats[j].depth) {
                        if ((formats[k].bitsPerPixel ==
                             xf86Screens[i]->formats[j].bitsPerPixel) &&
                            (formats[k].scanlinePad ==
                             xf86Screens[i]->formats[j].scanlinePad))
                            break;
                        FatalError("Inconsistent pixmap format for depth %d."
                                   "  Exiting\n", formats[k].depth);
                    }
                }
            }
        }
        formatsDone = TRUE;

        if (xf86Info.vtno >= 0) {
#define VT_ATOM_NAME         "XFree86_VT"
            Atom VTAtom = -1;
            CARD32 *VT = NULL;
            int ret;

            /* This memory needs to stay available until the screen has been
               initialized, and we can create the property for real.
             */
            if ((VT = malloc(sizeof(CARD32))) == NULL) {
                FatalError
                    ("Unable to make VT property - out of memory. Exiting...\n");
            }
            *VT = xf86Info.vtno;

            VTAtom = MakeAtom(VT_ATOM_NAME, sizeof(VT_ATOM_NAME) - 1, TRUE);

            for (i = 0, ret = Success; i < xf86NumScreens && ret == Success;
                 i++) {
                ret =
                    xf86RegisterRootWindowProperty(xf86Screens[i]->scrnIndex,
                                                   VTAtom, XA_INTEGER, 32, 1,
                                                   VT);
                if (ret != Success)
                    xf86DrvMsg(xf86Screens[i]->scrnIndex, X_WARNING,
                               "Failed to register VT property\n");
            }
        }

        if (SeatId) {
            Atom SeatAtom;

            SeatAtom =
                MakeAtom(SEAT_ATOM_NAME, sizeof(SEAT_ATOM_NAME) - 1, TRUE);

            for (i = 0; i < xf86NumScreens; i++) {
                int ret;

                ret = xf86RegisterRootWindowProperty(xf86Screens[i]->scrnIndex,
                                                     SeatAtom, XA_STRING, 8,
                                                     strlen(SeatId) + 1,
                                                     SeatId);
                if (ret != Success) {
                    xf86DrvMsg(xf86Screens[i]->scrnIndex, X_WARNING,
                               "Failed to register seat property\n");
                }
            }
        }

        /* If a screen uses depth 24, show what the pixmap format is */
        for (i = 0; i < xf86NumScreens; i++) {
            if (xf86Screens[i]->depth == 24) {
                xf86Msg(pix24From, "Depth 24 pixmap format is %d bpp\n",
                        PIX24TOBPP(pix24));
                break;
            }
        }
    }
    else {
        /*
         * serverGeneration != 1; some OSs have to do things here, too.
         */
        if (xorgHWOpenConsole)
            xf86OpenConsole();

#ifdef XF86PM
        /*
           should we reopen it here? We need to deal with an already opened
           device. We could leave this to the OS layer. For now we simply
           close it here
         */
        if (xf86OSPMClose)
            xf86OSPMClose();
        if ((xf86OSPMClose = xf86OSPMOpen()) != NULL)
            xf86MsgVerb(X_INFO, 3, "APM registered successfully\n");
#endif

        /* Make sure full I/O access is enabled */
        if (xorgHWAccess)
            xf86EnableIO();
    }

    /*
     * Use the previously collected parts to setup pScreenInfo
     */

    pScreenInfo->imageByteOrder = xf86Screens[0]->imageByteOrder;
    pScreenInfo->bitmapScanlinePad = xf86Screens[0]->bitmapScanlinePad;
    pScreenInfo->bitmapScanlineUnit = xf86Screens[0]->bitmapScanlineUnit;
    pScreenInfo->bitmapBitOrder = xf86Screens[0]->bitmapBitOrder;
    pScreenInfo->numPixmapFormats = numFormats;
    for (i = 0; i < numFormats; i++)
        pScreenInfo->formats[i] = formats[i];

    /* Make sure the server's VT is active */

    if (serverGeneration != 1) {
        xf86Resetting = TRUE;
        /* All screens are in the same state, so just check the first */
        if (!xf86Screens[0]->vtSema) {
#ifdef HAS_USL_VTS
            ioctl(xf86Info.consoleFd, VT_RELDISP, VT_ACKACQ);
#endif
            xf86AccessEnter();
            OsBlockSIGIO();
            sigio_blocked = TRUE;
        }
    }

    for (i = 0; i < xf86NumScreens; i++)
        if (!xf86ColormapAllocatePrivates(xf86Screens[i]))
            FatalError("Cannot register DDX private keys");

    if (!dixRegisterPrivateKey(&xf86ScreenKeyRec, PRIVATE_SCREEN, 0) ||
        !dixRegisterPrivateKey(&xf86CreateRootWindowKeyRec, PRIVATE_SCREEN, 0))
        FatalError("Cannot register DDX private keys");

    for (i = 0; i < xf86NumGPUScreens; i++) {
        ScrnInfoPtr pScrn = xf86GPUScreens[i];
        xf86VGAarbiterLock(pScrn);

        /*
         * Almost everything uses these defaults, and many of those that
         * don't, will wrap them.
         */
        pScrn->EnableDisableFBAccess = xf86EnableDisableFBAccess;
#ifdef XFreeXDGA
        pScrn->SetDGAMode = xf86SetDGAMode;
#endif
        pScrn->DPMSSet = NULL;
        pScrn->LoadPalette = NULL;
        pScrn->SetOverscan = NULL;
        pScrn->DriverFunc = NULL;
        pScrn->pScreen = NULL;
        scr_index = AddGPUScreen(pScrn->ScreenInit, argc, argv);
        xf86VGAarbiterUnlock(pScrn);
        if (scr_index == i) {
            dixSetPrivate(&screenInfo.gpuscreens[scr_index]->devPrivates,
                          xf86ScreenKey, xf86GPUScreens[i]);
            pScrn->pScreen = screenInfo.gpuscreens[scr_index];
            /* The driver should set this, but make sure it is set anyway */
            pScrn->vtSema = TRUE;
        } else {
            FatalError("AddScreen/ScreenInit failed for gpu driver %d %d\n", i, scr_index);
        }
    }

    for (i = 0; i < xf86NumScreens; i++) {
        xf86VGAarbiterLock(xf86Screens[i]);
        /*
         * Almost everything uses these defaults, and many of those that
         * don't, will wrap them.
         */
        xf86Screens[i]->EnableDisableFBAccess = xf86EnableDisableFBAccess;
#ifdef XFreeXDGA
        xf86Screens[i]->SetDGAMode = xf86SetDGAMode;
#endif
        xf86Screens[i]->DPMSSet = NULL;
        xf86Screens[i]->LoadPalette = NULL;
        xf86Screens[i]->SetOverscan = NULL;
        xf86Screens[i]->DriverFunc = NULL;
        xf86Screens[i]->pScreen = NULL;
        scr_index = AddScreen(xf86Screens[i]->ScreenInit, argc, argv);
        xf86VGAarbiterUnlock(xf86Screens[i]);
        if (scr_index == i) {
            /*
             * Hook in our ScrnInfoRec, and initialise some other pScreen
             * fields.
             */
            dixSetPrivate(&screenInfo.screens[scr_index]->devPrivates,
                          xf86ScreenKey, xf86Screens[i]);
            xf86Screens[i]->pScreen = screenInfo.screens[scr_index];
            /* The driver should set this, but make sure it is set anyway */
            xf86Screens[i]->vtSema = TRUE;
        }
        else {
            /* This shouldn't normally happen */
            FatalError("AddScreen/ScreenInit failed for driver %d\n", i);
        }

        DebugF("InitOutput - xf86Screens[%d]->pScreen = %p\n",
               i, xf86Screens[i]->pScreen);
        DebugF("xf86Screens[%d]->pScreen->CreateWindow = %p\n",
               i, xf86Screens[i]->pScreen->CreateWindow);

        dixSetPrivate(&screenInfo.screens[scr_index]->devPrivates,
                      xf86CreateRootWindowKey,
                      xf86Screens[i]->pScreen->CreateWindow);
        xf86Screens[i]->pScreen->CreateWindow = xf86CreateRootWindow;

        if (PictureGetSubpixelOrder(xf86Screens[i]->pScreen) == SubPixelUnknown) {
            xf86MonPtr DDC = (xf86MonPtr) (xf86Screens[i]->monitor->DDC);

            PictureSetSubpixelOrder(xf86Screens[i]->pScreen,
                                    DDC ?
                                    (DDC->features.input_type ?
                                     SubPixelHorizontalRGB : SubPixelNone) :
                                    SubPixelUnknown);
        }
#ifdef RANDR
        if (!xf86Info.disableRandR)
            xf86RandRInit(screenInfo.screens[scr_index]);
        xf86Msg(xf86Info.randRFrom, "RandR %s\n",
                xf86Info.disableRandR ? "disabled" : "enabled");
#endif
    }

    for (i = 0; i < xf86NumGPUScreens; i++)
        AttachUnboundGPU(xf86Screens[0]->pScreen, xf86GPUScreens[i]->pScreen);

    xf86VGAarbiterWrapFunctions();
    if (sigio_blocked)
        OsReleaseSIGIO();

    xf86InitOrigins();

    xf86Resetting = FALSE;
    xf86Initialising = FALSE;

    RegisterBlockAndWakeupHandlers((BlockHandlerProcPtr) NoopDDA, xf86Wakeup,
                                   NULL);
}

/**
 * Initialize all supported input devices present and referenced in the
 * xorg.conf.
 */
void
InitInput(int argc, char **argv)
{
    InputInfoPtr *pInfo;
    DeviceIntPtr dev;

    xf86Info.vtRequestsPending = FALSE;

    mieqInit();

    /* Initialize all configured input devices */
    for (pInfo = xf86ConfigLayout.inputs; pInfo && *pInfo; pInfo++) {
        (*pInfo)->options =
            xf86AddNewOption((*pInfo)->options, "driver", (*pInfo)->driver);
        (*pInfo)->options =
            xf86AddNewOption((*pInfo)->options, "identifier", (*pInfo)->name);
        /* If one fails, the others will too */
        if (NewInputDeviceRequest((*pInfo)->options, NULL, &dev) == BadAlloc)
            break;
    }

    config_init();
}

void
CloseInput(void)
{
    config_fini();
    mieqFini();
}

/*
 * OsVendorInit --
 *      OS/Vendor-specific initialisations.  Called from OsInit(), which
 *      is called by dix before establishing the well known sockets.
 */

void
OsVendorInit(void)
{
    static Bool beenHere = FALSE;

    signal(SIGCHLD, SIG_DFL);   /* Need to wait for child processes */

    if (!beenHere) {
        umask(022);
        xf86LogInit();
    }

    /* Set stderr to non-blocking. */
#ifndef O_NONBLOCK
#if defined(FNDELAY)
#define O_NONBLOCK FNDELAY
#elif defined(O_NDELAY)
#define O_NONBLOCK O_NDELAY
#endif

#ifdef O_NONBLOCK
    if (!beenHere) {
        if (xf86PrivsElevated()) {
            int status;

            status = fcntl(fileno(stderr), F_GETFL, 0);
            if (status != -1) {
                fcntl(fileno(stderr), F_SETFL, status | O_NONBLOCK);
            }
        }
    }
#endif
#endif

    beenHere = TRUE;
}

/*
 * ddxGiveUp --
 *      Device dependent cleanup. Called by by dix before normal server death.
 *      For SYSV386 we must switch the terminal back to normal mode. No error-
 *      checking here, since there should be restored as much as possible.
 */

void
ddxGiveUp(enum ExitCode error)
{
    int i;

    xf86VGAarbiterFini();

#ifdef XF86PM
    if (xf86OSPMClose)
        xf86OSPMClose();
    xf86OSPMClose = NULL;
#endif

    for (i = 0; i < xf86NumScreens; i++) {
        /*
         * zero all access functions to
         * trap calls when switched away.
         */
        xf86Screens[i]->vtSema = FALSE;
    }

#ifdef XFreeXDGA
    DGAShutdown();
#endif

    if (xorgHWOpenConsole)
        xf86CloseConsole();

    xf86CloseLog(error);

    /* If an unexpected signal was caught, dump a core for debugging */
    if (xf86Info.caughtSignal)
        OsAbort();
}

/*
 * AbortDDX --
 *      DDX - specific abort routine.  Called by AbortServer(). The attempt is
 *      made to restore all original setting of the displays. Also all devices
 *      are closed.
 */

void
AbortDDX(enum ExitCode error)
{
    int i;

    OsBlockSIGIO();

    /*
     * try to restore the original video state
     */
#ifdef DPMSExtension            /* Turn screens back on */
    if (DPMSPowerLevel != DPMSModeOn)
        DPMSSet(serverClient, DPMSModeOn);
#endif
    if (xf86Screens) {
        for (i = 0; i < xf86NumScreens; i++)
            if (xf86Screens[i]->vtSema) {
                /*
                 * if we are aborting before ScreenInit() has finished
                 * we might not have been wrapped yet. Therefore enable
                 * screen explicitely.
                 */
                xf86VGAarbiterLock(xf86Screens[i]);
                (xf86Screens[i]->LeaveVT) (xf86Screens[i]);
                xf86VGAarbiterUnlock(xf86Screens[i]);
            }
    }

    xf86AccessLeave();

    /*
     * This is needed for an abnormal server exit, since the normal exit stuff
     * MUST also be performed (i.e. the vt must be left in a defined state)
     */
    ddxGiveUp(error);
}

void
OsVendorFatalError(const char *f, va_list args)
{
#ifdef VENDORSUPPORT
    ErrorFSigSafe("\nPlease refer to your Operating System Vendor support "
                 "pages\nat %s for support on this crash.\n", VENDORSUPPORT);
#else
    ErrorFSigSafe("\nPlease consult the " XVENDORNAME " support \n\t at "
                 __VENDORDWEBSUPPORT__ "\n for help. \n");
#endif
    if (xf86LogFile && xf86LogFileWasOpened)
        ErrorFSigSafe("Please also check the log file at \"%s\" for additional "
                     "information.\n", xf86LogFile);
    ErrorFSigSafe("\n");
}

int
xf86SetVerbosity(int verb)
{
    int save = xf86Verbose;

    xf86Verbose = verb;
    LogSetParameter(XLOG_VERBOSITY, verb);
    return save;
}

int
xf86SetLogVerbosity(int verb)
{
    int save = xf86LogVerbose;

    xf86LogVerbose = verb;
    LogSetParameter(XLOG_FILE_VERBOSITY, verb);
    return save;
}

static void
xf86PrintDefaultModulePath(void)
{
    ErrorF("%s\n", DEFAULT_MODULE_PATH);
}

static void
xf86PrintDefaultLibraryPath(void)
{
    ErrorF("%s\n", DEFAULT_LIBRARY_PATH);
}

/*
 * ddxProcessArgument --
 *	Process device-dependent command line args. Returns 0 if argument is
 *      not device dependent, otherwise Count of number of elements of argv
 *      that are part of a device dependent commandline option.
 *
 */

/* ARGSUSED */
int
ddxProcessArgument(int argc, char **argv, int i)
{
#define CHECK_FOR_REQUIRED_ARGUMENT() \
    if (((i + 1) >= argc) || (!argv[i + 1])) { 				\
      ErrorF("Required argument to %s not specified\n", argv[i]); 	\
      UseMsg(); 							\
      FatalError("Required argument to %s not specified\n", argv[i]);	\
    }

    /* First the options that are not allowed with elevated privileges */
    if (!strcmp(argv[i], "-modulepath") || !strcmp(argv[i], "-logfile")) {
        if (xf86PrivsElevated()) {
            FatalError("The '%s' option cannot be used with "
                       "elevated privileges.\n", argv[i]);
        }
        else if (!strcmp(argv[i], "-modulepath")) {
            char *mp;

            CHECK_FOR_REQUIRED_ARGUMENT();
            mp = strdup(argv[i + 1]);
            if (!mp)
                FatalError("Can't allocate memory for ModulePath\n");
            xf86ModulePath = mp;
            xf86ModPathFrom = X_CMDLINE;
            return 2;
        }
        else if (!strcmp(argv[i], "-logfile")) {
            char *lf;

            CHECK_FOR_REQUIRED_ARGUMENT();
            lf = strdup(argv[i + 1]);
            if (!lf)
                FatalError("Can't allocate memory for LogFile\n");
            xf86LogFile = lf;
            xf86LogFileFrom = X_CMDLINE;
            return 2;
        }
    }
    if (!strcmp(argv[i], "-config") || !strcmp(argv[i], "-xf86config")) {
        CHECK_FOR_REQUIRED_ARGUMENT();
        if (xf86PrivsElevated() && !xf86PathIsSafe(argv[i + 1])) {
            FatalError("\nInvalid argument for %s\n"
                       "\tWith elevated privileges, the file specified with %s must be\n"
                       "\ta relative path and must not contain any \"..\" elements.\n"
                       "\tUsing default " __XCONFIGFILE__ " search path.\n\n",
                       argv[i], argv[i]);
        }
        xf86ConfigFile = argv[i + 1];
        return 2;
    }
    if (!strcmp(argv[i], "-configdir")) {
        CHECK_FOR_REQUIRED_ARGUMENT();
        if (xf86PrivsElevated() && !xf86PathIsSafe(argv[i + 1])) {
            FatalError("\nInvalid argument for %s\n"
                       "\tWith elevated privileges, the file specified with %s must be\n"
                       "\ta relative path and must not contain any \"..\" elements.\n"
                       "\tUsing default " __XCONFIGDIR__ " search path.\n\n",
                       argv[i], argv[i]);
        }
        xf86ConfigDir = argv[i + 1];
        return 2;
    }
    if (!strcmp(argv[i], "-flipPixels")) {
        xf86FlipPixels = TRUE;
        return 1;
    }
#ifdef XF86VIDMODE
    if (!strcmp(argv[i], "-disableVidMode")) {
        xf86VidModeDisabled = TRUE;
        return 1;
    }
    if (!strcmp(argv[i], "-allowNonLocalXvidtune")) {
        xf86VidModeAllowNonLocal = TRUE;
        return 1;
    }
#endif
    if (!strcmp(argv[i], "-allowMouseOpenFail")) {
        xf86AllowMouseOpenFail = TRUE;
        return 1;
    }
    if (!strcmp(argv[i], "-ignoreABI")) {
        LoaderSetOptions(LDR_OPT_ABI_MISMATCH_NONFATAL);
        return 1;
    }
    if (!strcmp(argv[i], "-verbose")) {
        if (++i < argc && argv[i]) {
            char *end;
            long val;

            val = strtol(argv[i], &end, 0);
            if (*end == '\0') {
                xf86SetVerbosity(val);
                return 2;
            }
        }
        xf86SetVerbosity(++xf86Verbose);
        return 1;
    }
    if (!strcmp(argv[i], "-logverbose")) {
        if (++i < argc && argv[i]) {
            char *end;
            long val;

            val = strtol(argv[i], &end, 0);
            if (*end == '\0') {
                xf86SetLogVerbosity(val);
                return 2;
            }
        }
        xf86SetLogVerbosity(++xf86LogVerbose);
        return 1;
    }
    if (!strcmp(argv[i], "-quiet")) {
        xf86SetVerbosity(-1);
        return 1;
    }
    if (!strcmp(argv[i], "-showconfig") || !strcmp(argv[i], "-version")) {
        xf86PrintBanner();
        exit(0);
    }
    if (!strcmp(argv[i], "-showDefaultModulePath")) {
        xf86PrintDefaultModulePath();
        exit(0);
    }
    if (!strcmp(argv[i], "-showDefaultLibPath")) {
        xf86PrintDefaultLibraryPath();
        exit(0);
    }
    /* Notice the -fp flag, but allow it to pass to the dix layer */
    if (!strcmp(argv[i], "-fp")) {
        xf86fpFlag = TRUE;
        return 0;
    }
    /* Notice the -bs flag, but allow it to pass to the dix layer */
    if (!strcmp(argv[i], "-bs")) {
        xf86bsDisableFlag = TRUE;
        return 0;
    }
    /* Notice the +bs flag, but allow it to pass to the dix layer */
    if (!strcmp(argv[i], "+bs")) {
        xf86bsEnableFlag = TRUE;
        return 0;
    }
    /* Notice the -s flag, but allow it to pass to the dix layer */
    if (!strcmp(argv[i], "-s")) {
        xf86sFlag = TRUE;
        return 0;
    }
    if (!strcmp(argv[i], "-pixmap24")) {
        xf86Pix24 = Pix24Use24;
        return 1;
    }
    if (!strcmp(argv[i], "-pixmap32")) {
        xf86Pix24 = Pix24Use32;
        return 1;
    }
    if (!strcmp(argv[i], "-fbbpp")) {
        int bpp;

        CHECK_FOR_REQUIRED_ARGUMENT();
        if (sscanf(argv[++i], "%d", &bpp) == 1) {
            xf86FbBpp = bpp;
            return 2;
        }
        else {
            ErrorF("Invalid fbbpp\n");
            return 0;
        }
    }
    if (!strcmp(argv[i], "-depth")) {
        int depth;

        CHECK_FOR_REQUIRED_ARGUMENT();
        if (sscanf(argv[++i], "%d", &depth) == 1) {
            xf86Depth = depth;
            return 2;
        }
        else {
            ErrorF("Invalid depth\n");
            return 0;
        }
    }
    if (!strcmp(argv[i], "-weight")) {
        int red, green, blue;

        CHECK_FOR_REQUIRED_ARGUMENT();
        if (sscanf(argv[++i], "%1d%1d%1d", &red, &green, &blue) == 3) {
            xf86Weight.red = red;
            xf86Weight.green = green;
            xf86Weight.blue = blue;
            return 2;
        }
        else {
            ErrorF("Invalid weighting\n");
            return 0;
        }
    }
    if (!strcmp(argv[i], "-gamma") || !strcmp(argv[i], "-rgamma") ||
        !strcmp(argv[i], "-ggamma") || !strcmp(argv[i], "-bgamma")) {
        double gamma;

        CHECK_FOR_REQUIRED_ARGUMENT();
        if (sscanf(argv[++i], "%lf", &gamma) == 1) {
            if (gamma < GAMMA_MIN || gamma > GAMMA_MAX) {
                ErrorF("gamma out of range, only  %.2f <= gamma_value <= %.1f"
                       " is valid\n", GAMMA_MIN, GAMMA_MAX);
                return 0;
            }
            if (!strcmp(argv[i - 1], "-gamma"))
                xf86Gamma.red = xf86Gamma.green = xf86Gamma.blue = gamma;
            else if (!strcmp(argv[i - 1], "-rgamma"))
                xf86Gamma.red = gamma;
            else if (!strcmp(argv[i - 1], "-ggamma"))
                xf86Gamma.green = gamma;
            else if (!strcmp(argv[i - 1], "-bgamma"))
                xf86Gamma.blue = gamma;
            return 2;
        }
    }
    if (!strcmp(argv[i], "-layout")) {
        CHECK_FOR_REQUIRED_ARGUMENT();
        xf86LayoutName = argv[++i];
        return 2;
    }
    if (!strcmp(argv[i], "-screen")) {
        CHECK_FOR_REQUIRED_ARGUMENT();
        xf86ScreenName = argv[++i];
        return 2;
    }
    if (!strcmp(argv[i], "-pointer")) {
        CHECK_FOR_REQUIRED_ARGUMENT();
        xf86PointerName = argv[++i];
        return 2;
    }
    if (!strcmp(argv[i], "-keyboard")) {
        CHECK_FOR_REQUIRED_ARGUMENT();
        xf86KeyboardName = argv[++i];
        return 2;
    }
    if (!strcmp(argv[i], "-nosilk")) {
        xf86silkenMouseDisableFlag = TRUE;
        return 1;
    }
#ifdef HAVE_ACPI
    if (!strcmp(argv[i], "-noacpi")) {
        xf86acpiDisableFlag = TRUE;
        return 1;
    }
#endif
    if (!strcmp(argv[i], "-configure")) {
        if (getuid() != 0 && geteuid() == 0) {
            ErrorF("The '-configure' option can only be used by root.\n");
            exit(1);
        }
        xf86DoConfigure = TRUE;
        xf86AllowMouseOpenFail = TRUE;
        return 1;
    }
    if (!strcmp(argv[i], "-showopts")) {
        if (getuid() != 0 && geteuid() == 0) {
            ErrorF("The '-showopts' option can only be used by root.\n");
            exit(1);
        }
        xf86DoShowOptions = TRUE;
        return 1;
    }
#ifdef XSERVER_LIBPCIACCESS
    if (!strcmp(argv[i], "-isolateDevice")) {
        CHECK_FOR_REQUIRED_ARGUMENT();
        if (strncmp(argv[++i], "PCI:", 4)) {
            FatalError("Bus types other than PCI not yet isolable\n");
        }
        xf86PciIsolateDevice(argv[i]);
        return 2;
    }
#endif
    /* Notice cmdline xkbdir, but pass to dix as well */
    if (!strcmp(argv[i], "-xkbdir")) {
        xf86xkbdirFlag = TRUE;
        return 0;
    }
    if (!strcmp(argv[i], "-novtswitch")) {
        xf86Info.autoVTSwitch = FALSE;
        return 1;
    }
    if (!strcmp(argv[i], "-sharevts")) {
        xf86Info.ShareVTs = TRUE;
        return 1;
    }

    /* OS-specific processing */
    return xf86ProcessArgument(argc, argv, i);
}

/*
 * ddxUseMsg --
 *	Print out correct use of device dependent commandline options.
 *      Maybe the user now knows what really to do ...
 */

void
ddxUseMsg(void)
{
    ErrorF("\n");
    ErrorF("\n");
    ErrorF("Device Dependent Usage\n");
    if (!xf86PrivsElevated()) {
        ErrorF("-modulepath paths      specify the module search path\n");
        ErrorF("-logfile file          specify a log file name\n");
        ErrorF("-configure             probe for devices and write an "
               __XCONFIGFILE__ "\n");
        ErrorF
            ("-showopts              print available options for all installed drivers\n");
    }
    ErrorF
        ("-config file           specify a configuration file, relative to the\n");
    ErrorF("                       " __XCONFIGFILE__
           " search path, only root can use absolute\n");
    ErrorF
        ("-configdir dir         specify a configuration directory, relative to the\n");
    ErrorF("                       " __XCONFIGDIR__
           " search path, only root can use absolute\n");
    ErrorF("-verbose [n]           verbose startup messages\n");
    ErrorF("-logverbose [n]        verbose log messages\n");
    ErrorF("-quiet                 minimal startup messages\n");
    ErrorF("-pixmap24              use 24bpp pixmaps for depth 24\n");
    ErrorF("-pixmap32              use 32bpp pixmaps for depth 24\n");
    ErrorF("-fbbpp n               set bpp for the framebuffer. Default: 8\n");
    ErrorF("-depth n               set colour depth. Default: 8\n");
    ErrorF
        ("-gamma f               set gamma value (0.1 < f < 10.0) Default: 1.0\n");
    ErrorF("-rgamma f              set gamma value for red phase\n");
    ErrorF("-ggamma f              set gamma value for green phase\n");
    ErrorF("-bgamma f              set gamma value for blue phase\n");
    ErrorF
        ("-weight nnn            set RGB weighting at 16 bpp.  Default: 565\n");
    ErrorF("-layout name           specify the ServerLayout section name\n");
    ErrorF("-screen name           specify the Screen section name\n");
    ErrorF
        ("-keyboard name         specify the core keyboard InputDevice name\n");
    ErrorF
        ("-pointer name          specify the core pointer InputDevice name\n");
    ErrorF("-nosilk                disable Silken Mouse\n");
    ErrorF("-flipPixels            swap default black/white Pixel values\n");
#ifdef XF86VIDMODE
    ErrorF("-disableVidMode        disable mode adjustments with xvidtune\n");
    ErrorF
        ("-allowNonLocalXvidtune allow xvidtune to be run as a non-local client\n");
#endif
    ErrorF
        ("-allowMouseOpenFail    start server even if the mouse can't be initialized\n");
    ErrorF("-ignoreABI             make module ABI mismatches non-fatal\n");
#ifdef XSERVER_LIBPCIACCESS
    ErrorF
        ("-isolateDevice bus_id  restrict device resets to bus_id (PCI only)\n");
#endif
    ErrorF("-version               show the server version\n");
    ErrorF("-showDefaultModulePath show the server default module path\n");
    ErrorF("-showDefaultLibPath    show the server default library path\n");
    ErrorF
        ("-novtswitch            don't automatically switch VT at reset & exit\n");
    ErrorF("-sharevts              share VTs with another X server\n");
    /* OS-specific usage */
    xf86UseMsg();
    ErrorF("\n");
}

/*
 * xf86LoadModules iterates over a list that is being passed in.
 */
Bool
xf86LoadModules(char **list, pointer *optlist)
{
    int errmaj, errmin;
    pointer opt;
    int i;
    char *name;
    Bool failed = FALSE;

    if (!list)
        return TRUE;

    for (i = 0; list[i] != NULL; i++) {

        /* Normalise the module name */
        name = xf86NormalizeName(list[i]);

        /* Skip empty names */
        if (name == NULL || *name == '\0') {
            free(name);
            continue;
        }

        /* Replace obsolete keyboard driver with kbd */
        if (!xf86NameCmp(name, "keyboard")) {
            strcpy(name, "kbd");
        }

        if (optlist)
            opt = optlist[i];
        else
            opt = NULL;

        if (!LoadModule(name, NULL, NULL, NULL, opt, NULL, &errmaj, &errmin)) {
            LoaderErrorMsg(NULL, name, errmaj, errmin);
            failed = TRUE;
        }
        free(name);
    }
    return !failed;
}

/* Pixmap format stuff */

PixmapFormatPtr
xf86GetPixFormat(ScrnInfoPtr pScrn, int depth)
{
    int i;
    static PixmapFormatRec format;      /* XXX not reentrant */

    /*
     * When the formats[] list initialisation isn't complete, check the
     * depth 24 pixmap config/cmdline options and screen-specified formats.
     */

    if (!formatsDone) {
        if (depth == 24) {
            Pix24Flags pix24 = Pix24DontCare;

            format.depth = 24;
            format.scanlinePad = BITMAP_SCANLINE_PAD;
            if (xf86Info.pixmap24 != Pix24DontCare)
                pix24 = xf86Info.pixmap24;
            else if (pScrn->pixmap24 != Pix24DontCare)
                pix24 = pScrn->pixmap24;
            if (pix24 == Pix24Use24)
                format.bitsPerPixel = 24;
            else
                format.bitsPerPixel = 32;
            return &format;
        }
    }

    for (i = 0; i < numFormats; i++)
        if (formats[i].depth == depth)
            break;
    if (i != numFormats)
        return &formats[i];
    else if (!formatsDone) {
        /* Check for screen-specified formats */
        for (i = 0; i < pScrn->numFormats; i++)
            if (pScrn->formats[i].depth == depth)
                break;
        if (i != pScrn->numFormats)
            return &pScrn->formats[i];
    }
    return NULL;
}

int
xf86GetBppFromDepth(ScrnInfoPtr pScrn, int depth)
{
    PixmapFormatPtr format;

    format = xf86GetPixFormat(pScrn, depth);
    if (format)
        return format->bitsPerPixel;
    else
        return 0;
}

#ifdef DDXBEFORERESET
void
ddxBeforeReset(void)
{
}
#endif
