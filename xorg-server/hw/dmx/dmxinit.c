/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
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
 * Authors:
 *   Kevin E. Martin <kem@redhat.com>
 *   David H. Dawes <dawes@xfree86.org>
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Provide expected functions for initialization from the ddx layer and
 * global variables for the DMX server. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxinit.h"
#include "dmxsync.h"
#include "dmxlog.h"
#include "dmxinput.h"
#include "dmxscrinit.h"
#include "dmxcursor.h"
#include "dmxfont.h"
#include "config/dmxconfig.h"
#include "dmxcb.h"
#include "dmxprop.h"
#include "dmxstat.h"
#include "dmxpict.h"

#include <X11/Xos.h>            /* For gettimeofday */
#include <X11/Xmu/SysUtil.h>    /* For XmuGetHostname */
#include "dixstruct.h"
#ifdef PANORAMIX
#include "panoramiXsrv.h"
#endif

#include <signal.h>             /* For SIGQUIT */

#ifdef GLXEXT
#include <GL/glx.h>
#include <GL/glxint.h>
#include "dmx_glxvisuals.h"
#include "glx_extinit.h"
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#endif                          /* GLXEXT */

#include <X11/extensions/dmxproto.h>

/* Global variables available to all Xserver/hw/dmx routines. */
int dmxNumScreens;
DMXScreenInfo *dmxScreens;

int dmxNumInputs;
DMXInputInfo *dmxInputs;

XErrorEvent dmxLastErrorEvent;
Bool dmxErrorOccurred = FALSE;

char *dmxFontPath = NULL;

Bool dmxOffScreenOpt = TRUE;

Bool dmxSubdividePrimitives = TRUE;

Bool dmxLazyWindowCreation = TRUE;

Bool dmxUseXKB = TRUE;

int dmxDepth = 0;

#ifndef GLXEXT
static Bool dmxGLXProxy = FALSE;
#else
Bool dmxGLXProxy = TRUE;

Bool dmxGLXSwapGroupSupport = TRUE;

Bool dmxGLXSyncSwap = FALSE;

Bool dmxGLXFinishSwap = FALSE;
#endif

RESTYPE RRProviderType = 0;

Bool dmxIgnoreBadFontPaths = FALSE;

Bool dmxAddRemoveScreens = FALSE;

/* dmxErrorHandler catches errors that occur when calling one of the
 * back-end servers.  Some of this code is based on _XPrintDefaultError
 * in xc/lib/X11/XlibInt.c */
static int
dmxErrorHandler(Display * dpy, XErrorEvent * ev)
{
#define DMX_ERROR_BUF_SIZE 256
    /* RATS: these buffers are only used in
     * length-limited calls. */
    char buf[DMX_ERROR_BUF_SIZE];
    char request[DMX_ERROR_BUF_SIZE];
    _XExtension *ext = NULL;

    dmxErrorOccurred = TRUE;
    dmxLastErrorEvent = *ev;

    XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
    dmxLog(dmxWarning, "dmxErrorHandler: %s\n", buf);

    /* Find major opcode name */
    if (ev->request_code < 128) {
        snprintf(request, sizeof(request), "%d", ev->request_code);
        XGetErrorDatabaseText(dpy, "XRequest", request, "", buf, sizeof(buf));
    }
    else {
        for (ext = dpy->ext_procs;
             ext && ext->codes.major_opcode != ev->request_code;
             ext = ext->next);
        if (ext)
            strlcpy(buf, ext->name, sizeof(buf));
        else
            buf[0] = '\0';
    }
    dmxLog(dmxWarning, "                 Major opcode: %d (%s)\n",
           ev->request_code, buf);

    /* Find minor opcode name */
    if (ev->request_code >= 128 && ext) {
        snprintf(request, sizeof(request), "%d", ev->request_code);
        snprintf(request, sizeof(request), "%s.%d", ext->name, ev->minor_code);
        XGetErrorDatabaseText(dpy, "XRequest", request, "", buf, sizeof(buf));
        dmxLog(dmxWarning, "                 Minor opcode: %d (%s)\n",
               ev->minor_code, buf);
    }

    /* Provide value information */
    switch (ev->error_code) {
    case BadValue:
        dmxLog(dmxWarning, "                 Value:        0x%x\n",
               (unsigned int) ev->resourceid);
        break;
    case BadAtom:
        dmxLog(dmxWarning, "                 AtomID:       0x%x\n",
               (unsigned int) ev->resourceid);
        break;
    default:
        dmxLog(dmxWarning, "                 ResourceID:   0x%x\n",
               (unsigned int) ev->resourceid);
        break;
    }

    /* Provide serial number information */
    dmxLog(dmxWarning, "                 Failed serial number:  %d\n",
           (unsigned int) ev->serial);
    dmxLog(dmxWarning, "                 Current serial number: %d\n",
           (unsigned int) dpy->request);
    return 0;
}

#ifdef GLXEXT
static int
dmxNOPErrorHandler(Display * dpy, XErrorEvent * ev)
{
    return 0;
}
#endif

Bool
dmxOpenDisplay(DMXScreenInfo * dmxScreen)
{
    if (!(dmxScreen->beDisplay = XOpenDisplay(dmxScreen->name)))
        return FALSE;

    dmxPropertyDisplay(dmxScreen);
    return TRUE;
}

void
dmxSetErrorHandler(DMXScreenInfo * dmxScreen)
{
    XSetErrorHandler(dmxErrorHandler);
}

static void
dmxPrintScreenInfo(DMXScreenInfo * dmxScreen)
{
    XWindowAttributes attribs;
    int ndepths = 0, *depths = NULL;
    int i;
    Display *dpy = dmxScreen->beDisplay;
    Screen *s = DefaultScreenOfDisplay(dpy);
    int scr = DefaultScreen(dpy);

    XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &attribs);
    if (!(depths = XListDepths(dpy, scr, &ndepths)))
        ndepths = 0;

    dmxLogOutput(dmxScreen, "Name of display: %s\n", DisplayString(dpy));
    dmxLogOutput(dmxScreen, "Version number:  %d.%d\n",
                 ProtocolVersion(dpy), ProtocolRevision(dpy));
    dmxLogOutput(dmxScreen, "Vendor string:   %s\n", ServerVendor(dpy));
    if (!strstr(ServerVendor(dpy), "XFree86")) {
        dmxLogOutput(dmxScreen, "Vendor release:  %d\n", VendorRelease(dpy));
    }
    else {
        /* This code based on xdpyinfo.c */
        int v = VendorRelease(dpy);
        int major = -1, minor = -1, patch = -1, subpatch = -1;

        if (v < 336)
            major = v / 100, minor = (v / 10) % 10, patch = v % 10;
        else if (v < 3900) {
            major = v / 1000;
            minor = (v / 100) % 10;
            if (((v / 10) % 10) || (v % 10)) {
                patch = (v / 10) % 10;
                if (v % 10)
                    subpatch = v % 10;
            }
        }
        else if (v < 40000000) {
            major = v / 1000;
            minor = (v / 10) % 10;
            if (v % 10)
                patch = v % 10;
        }
        else {
            major = v / 10000000;
            minor = (v / 100000) % 100;
            patch = (v / 1000) % 100;
            if (v % 1000)
                subpatch = v % 1000;
        }
        dmxLogOutput(dmxScreen, "Vendor release:  %d (XFree86 version: %d.%d",
                     v, major, minor);
        if (patch > 0)
            dmxLogOutputCont(dmxScreen, ".%d", patch);
        if (subpatch > 0)
            dmxLogOutputCont(dmxScreen, ".%d", subpatch);
        dmxLogOutputCont(dmxScreen, ")\n");
    }

    dmxLogOutput(dmxScreen, "Dimensions:      %dx%d pixels\n",
                 attribs.width, attribs.height);
    dmxLogOutput(dmxScreen, "%d depths on screen %d: ", ndepths, scr);
    for (i = 0; i < ndepths; i++)
        dmxLogOutputCont(dmxScreen, "%c%d", i ? ',' : ' ', depths[i]);
    dmxLogOutputCont(dmxScreen, "\n");
    dmxLogOutput(dmxScreen, "Depth of root window:  %d plane%s (%d)\n",
                 attribs.depth, attribs.depth == 1 ? "" : "s",
                 DisplayPlanes(dpy, scr));
    dmxLogOutput(dmxScreen, "Number of colormaps:   %d min, %d max\n",
                 MinCmapsOfScreen(s), MaxCmapsOfScreen(s));
    dmxLogOutput(dmxScreen, "Options: backing-store %s, save-unders %s\n",
                 (DoesBackingStore(s) == NotUseful) ? "no" :
                 ((DoesBackingStore(s) == Always) ? "yes" : "when mapped"),
                 DoesSaveUnders(s) ? "yes" : "no");
    dmxLogOutput(dmxScreen, "Window Manager running: %s\n",
                 (dmxScreen->WMRunningOnBE) ? "yes" : "no");

    if (dmxScreen->WMRunningOnBE) {
        dmxLogOutputWarning(dmxScreen,
                            "Window manager running "
                            "-- colormaps not supported\n");
    }
    XFree(depths);
}

void
dmxGetScreenAttribs(DMXScreenInfo * dmxScreen)
{
    XWindowAttributes attribs;
    Display *dpy = dmxScreen->beDisplay;

#ifdef GLXEXT
    int dummy;
#endif

    XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &attribs);

    dmxScreen->beWidth = attribs.width;
    dmxScreen->beHeight = attribs.height;

    /* Fill in missing geometry information */
    if (dmxScreen->scrnXSign < 0) {
        if (dmxScreen->scrnWidth) {
            dmxScreen->scrnX = (attribs.width - dmxScreen->scrnWidth
                                - dmxScreen->scrnX);
        }
        else {
            dmxScreen->scrnWidth = attribs.width - dmxScreen->scrnX;
            dmxScreen->scrnX = 0;
        }
    }
    if (dmxScreen->scrnYSign < 0) {
        if (dmxScreen->scrnHeight) {
            dmxScreen->scrnY = (attribs.height - dmxScreen->scrnHeight
                                - dmxScreen->scrnY);
        }
        else {
            dmxScreen->scrnHeight = attribs.height - dmxScreen->scrnY;
            dmxScreen->scrnY = 0;
        }
    }
    if (!dmxScreen->scrnWidth)
        dmxScreen->scrnWidth = attribs.width - dmxScreen->scrnX;
    if (!dmxScreen->scrnHeight)
        dmxScreen->scrnHeight = attribs.height - dmxScreen->scrnY;

    if (!dmxScreen->rootWidth)
        dmxScreen->rootWidth = dmxScreen->scrnWidth;
    if (!dmxScreen->rootHeight)
        dmxScreen->rootHeight = dmxScreen->scrnHeight;
    if (dmxScreen->rootWidth + dmxScreen->rootX > dmxScreen->scrnWidth)
        dmxScreen->rootWidth = dmxScreen->scrnWidth - dmxScreen->rootX;
    if (dmxScreen->rootHeight + dmxScreen->rootY > dmxScreen->scrnHeight)
        dmxScreen->rootHeight = dmxScreen->scrnHeight - dmxScreen->rootY;

    /* FIXME: Get these from the back-end server */
    dmxScreen->beXDPI = 75;
    dmxScreen->beYDPI = 75;

    dmxScreen->beDepth = attribs.depth; /* FIXME: verify that this
                                         * works always.  In
                                         * particular, this will work
                                         * well for depth=16, will fail
                                         * because of colormap issues
                                         * at depth 8.  More work needs
                                         * to be done here. */

    if (dmxScreen->beDepth <= 8)
        dmxScreen->beBPP = 8;
    else if (dmxScreen->beDepth <= 16)
        dmxScreen->beBPP = 16;
    else
        dmxScreen->beBPP = 32;

#ifdef GLXEXT
    /* get the majorOpcode for the back-end GLX extension */
    XQueryExtension(dpy, "GLX", &dmxScreen->glxMajorOpcode,
                    &dummy, &dmxScreen->glxErrorBase);
#endif

    dmxPrintScreenInfo(dmxScreen);
    dmxLogOutput(dmxScreen, "%dx%d+%d+%d on %dx%d at depth=%d, bpp=%d\n",
                 dmxScreen->scrnWidth, dmxScreen->scrnHeight,
                 dmxScreen->scrnX, dmxScreen->scrnY,
                 dmxScreen->beWidth, dmxScreen->beHeight,
                 dmxScreen->beDepth, dmxScreen->beBPP);
    if (dmxScreen->beDepth == 8)
        dmxLogOutputWarning(dmxScreen,
                            "Support for depth == 8 is not complete\n");
}

Bool
dmxGetVisualInfo(DMXScreenInfo * dmxScreen)
{
    int i;
    XVisualInfo visinfo;

    visinfo.screen = DefaultScreen(dmxScreen->beDisplay);
    dmxScreen->beVisuals = XGetVisualInfo(dmxScreen->beDisplay,
                                          VisualScreenMask,
                                          &visinfo, &dmxScreen->beNumVisuals);

    dmxScreen->beDefVisualIndex = -1;

    if (defaultColorVisualClass >= 0 || dmxDepth > 0) {
        for (i = 0; i < dmxScreen->beNumVisuals; i++)
            if (defaultColorVisualClass >= 0) {
                if (dmxScreen->beVisuals[i].class == defaultColorVisualClass) {
                    if (dmxDepth > 0) {
                        if (dmxScreen->beVisuals[i].depth == dmxDepth) {
                            dmxScreen->beDefVisualIndex = i;
                            break;
                        }
                    }
                    else {
                        dmxScreen->beDefVisualIndex = i;
                        break;
                    }
                }
            }
            else if (dmxScreen->beVisuals[i].depth == dmxDepth) {
                dmxScreen->beDefVisualIndex = i;
                break;
            }
    }
    else {
        visinfo.visualid =
            XVisualIDFromVisual(DefaultVisual(dmxScreen->beDisplay,
                                              visinfo.screen));

        for (i = 0; i < dmxScreen->beNumVisuals; i++)
            if (visinfo.visualid == dmxScreen->beVisuals[i].visualid) {
                dmxScreen->beDefVisualIndex = i;
                break;
            }
    }

    for (i = 0; i < dmxScreen->beNumVisuals; i++)
        dmxLogVisual(dmxScreen, &dmxScreen->beVisuals[i],
                     (i == dmxScreen->beDefVisualIndex));

    return dmxScreen->beDefVisualIndex >= 0;
}

void
dmxGetColormaps(DMXScreenInfo * dmxScreen)
{
    int i;

    dmxScreen->beNumDefColormaps = dmxScreen->beNumVisuals;
    dmxScreen->beDefColormaps = malloc(dmxScreen->beNumDefColormaps *
                                       sizeof(*dmxScreen->beDefColormaps));

    for (i = 0; i < dmxScreen->beNumDefColormaps; i++)
        dmxScreen->beDefColormaps[i] =
            XCreateColormap(dmxScreen->beDisplay,
                            DefaultRootWindow(dmxScreen->beDisplay),
                            dmxScreen->beVisuals[i].visual, AllocNone);

    dmxScreen->beBlackPixel = BlackPixel(dmxScreen->beDisplay,
                                         DefaultScreen(dmxScreen->beDisplay));
    dmxScreen->beWhitePixel = WhitePixel(dmxScreen->beDisplay,
                                         DefaultScreen(dmxScreen->beDisplay));
}

void
dmxGetPixmapFormats(DMXScreenInfo * dmxScreen)
{
    dmxScreen->beDepths =
        XListDepths(dmxScreen->beDisplay, DefaultScreen(dmxScreen->beDisplay),
                    &dmxScreen->beNumDepths);

    dmxScreen->bePixmapFormats =
        XListPixmapFormats(dmxScreen->beDisplay,
                           &dmxScreen->beNumPixmapFormats);
}

static Bool
dmxSetPixmapFormats(ScreenInfo * pScreenInfo, DMXScreenInfo * dmxScreen)
{
    XPixmapFormatValues *bePixmapFormat;
    PixmapFormatRec *format;
    int i, j;

    pScreenInfo->imageByteOrder = ImageByteOrder(dmxScreen->beDisplay);
    pScreenInfo->bitmapScanlineUnit = BitmapUnit(dmxScreen->beDisplay);
    pScreenInfo->bitmapScanlinePad = BitmapPad(dmxScreen->beDisplay);
    pScreenInfo->bitmapBitOrder = BitmapBitOrder(dmxScreen->beDisplay);

    pScreenInfo->numPixmapFormats = 0;
    for (i = 0; i < dmxScreen->beNumPixmapFormats; i++) {
        bePixmapFormat = &dmxScreen->bePixmapFormats[i];
        for (j = 0; j < dmxScreen->beNumDepths; j++)
            if ((bePixmapFormat->depth == 1) ||
                (bePixmapFormat->depth == dmxScreen->beDepths[j])) {
                format = &pScreenInfo->formats[pScreenInfo->numPixmapFormats];

                format->depth = bePixmapFormat->depth;
                format->bitsPerPixel = bePixmapFormat->bits_per_pixel;
                format->scanlinePad = bePixmapFormat->scanline_pad;

                pScreenInfo->numPixmapFormats++;
                break;
            }
    }

    return TRUE;
}

void
dmxCheckForWM(DMXScreenInfo * dmxScreen)
{
    Status status;
    XWindowAttributes xwa;

    status = XGetWindowAttributes(dmxScreen->beDisplay,
                                  DefaultRootWindow(dmxScreen->beDisplay),
                                  &xwa);
    dmxScreen->WMRunningOnBE =
        (status &&
         ((xwa.all_event_masks & SubstructureRedirectMask) ||
          (xwa.all_event_masks & SubstructureNotifyMask)));
}

/** Initialize the display and collect relevant information about the
 *  display properties */
static void
dmxDisplayInit(DMXScreenInfo * dmxScreen)
{
    if (!dmxOpenDisplay(dmxScreen))
        dmxLog(dmxFatal,
               "dmxOpenDisplay: Unable to open display %s\n", dmxScreen->name);

    dmxSetErrorHandler(dmxScreen);
    dmxCheckForWM(dmxScreen);
    dmxGetScreenAttribs(dmxScreen);

    if (!dmxGetVisualInfo(dmxScreen))
        dmxLog(dmxFatal, "dmxGetVisualInfo: No matching visuals found\n");

    dmxGetColormaps(dmxScreen);
    dmxGetPixmapFormats(dmxScreen);
}

/* If this doesn't compile, just add || defined(yoursystem) to the line
 * below.  This information is to help with bug reports and is not
 * critical. */
#if !defined(_POSIX_SOURCE)
static const char *
dmxExecOS(void)
{
    return "";
}
#else
#include <sys/utsname.h>
static const char *
dmxExecOS(void)
{
    static char buffer[128];
    static int initialized = 0;
    struct utsname u;

    if (!initialized++) {
        memset(buffer, 0, sizeof(buffer));
        uname(&u);
        snprintf(buffer, sizeof(buffer) - 1, "%s %s %s",
                 u.sysname, u.release, u.version);
    }
    return buffer;
}
#endif

static const char *
dmxBuildCompiler(void)
{
    static char buffer[128];
    static int initialized = 0;

    if (!initialized++) {
        memset(buffer, 0, sizeof(buffer));
#if defined(__GNUC__) && defined(__GNUC_MINOR__) &&defined(__GNUC_PATCHLEVEL__)
        snprintf(buffer, sizeof(buffer) - 1, "gcc %d.%d.%d",
                 __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
    }
    return buffer;
}

static const char *
dmxExecHost(void)
{
    static char buffer[128];
    static int initialized = 0;

    if (!initialized++) {
        memset(buffer, 0, sizeof(buffer));
        XmuGetHostname(buffer, sizeof(buffer) - 1);
    }
    return buffer;
}

static void dmxAddExtensions(Bool glxSupported)
{
    const ExtensionModule dmxExtensions[] = {
        { DMXExtensionInit, DMX_EXTENSION_NAME, NULL },
#ifdef GLXEXT
        { GlxExtensionInit, "GLX", &glxSupported },
#endif
    };

    LoadExtensionList(dmxExtensions, ARRAY_SIZE(dmxExtensions), TRUE);
}

/** This routine is called in Xserver/dix/main.c from \a main(). */
void
InitOutput(ScreenInfo * pScreenInfo, int argc, char *argv[])
{
    int i;
    static unsigned long dmxGeneration = 0;

#ifdef GLXEXT
    static Bool glxSupported = TRUE;
#else
    const Bool glxSupported = FALSE;
#endif

    if (dmxGeneration != serverGeneration) {
        int vendrel = VENDOR_RELEASE;
        int major, minor, year, month, day;

        dmxGeneration = serverGeneration;

        major = vendrel / 100000000;
        vendrel -= major * 100000000;
        minor = vendrel / 1000000;
        vendrel -= minor * 1000000;
        year = vendrel / 10000;
        vendrel -= year * 10000;
        month = vendrel / 100;
        vendrel -= month * 100;
        day = vendrel;

        /* Add other epoch tests here */
        if (major > 0 && minor > 0)
            year += 2000;

        dmxLog(dmxInfo, "Generation:         %lu\n", dmxGeneration);
        dmxLog(dmxInfo, "DMX version:        %d.%d.%02d%02d%02d (%s)\n",
               major, minor, year, month, day, VENDOR_STRING);

        SetVendorRelease(VENDOR_RELEASE);
        SetVendorString(VENDOR_STRING);

        if (dmxGeneration == 1) {
            dmxLog(dmxInfo, "DMX Build OS:       %s (%s)\n", OSNAME, OSVENDOR);
            dmxLog(dmxInfo, "DMX Build Compiler: %s\n", dmxBuildCompiler());
            dmxLog(dmxInfo, "DMX Execution OS:   %s\n", dmxExecOS());
            dmxLog(dmxInfo, "DMX Execution Host: %s\n", dmxExecHost());
        }
        dmxLog(dmxInfo, "MAXSCREENS:         %d\n", MAXSCREENS);

        for (i = 0; i < dmxNumScreens; i++) {
            if (dmxScreens[i].beDisplay)
                dmxLog(dmxWarning, "Display \"%s\" still open\n",
                       dmxScreens[i].name);
            dmxStatFree(dmxScreens[i].stat);
            dmxScreens[i].stat = NULL;
        }
        for (i = 0; i < dmxNumInputs; i++)
            dmxInputFree(&dmxInputs[i]);
        free(dmxScreens);
        free(dmxInputs);
        dmxScreens = NULL;
        dmxInputs = NULL;
        dmxNumScreens = 0;
        dmxNumInputs = 0;
    }

    /* Make sure that the command-line arguments are sane. */
    if (dmxAddRemoveScreens && dmxGLXProxy) {
        /* Currently it is not possible to support GLX and Render
         * extensions with dynamic screen addition/removal due to the
         * state that each extension keeps, which cannot be restored. */
        dmxLog(dmxWarning,
               "GLX Proxy and Render extensions do not yet support dynamic\n");
        dmxLog(dmxWarning,
               "screen addition and removal.  Please specify -noglxproxy\n");
        dmxLog(dmxWarning,
               "and -norender on the command line or in the configuration\n");
        dmxLog(dmxWarning,
               "file to disable these two extensions if you wish to use\n");
        dmxLog(dmxWarning,
               "the dynamic addition and removal of screens support.\n");
        dmxLog(dmxFatal,
               "Dynamic screen addition/removal error (see above).\n");
    }

    /* ddxProcessArgument has been called at this point, but any data
     * from the configuration file has not been applied.  Do so, and be
     * sure we have at least one back-end display. */
    dmxConfigConfigure();
    if (!dmxNumScreens)
        dmxLog(dmxFatal, "InitOutput: no back-end displays found\n");
    if (!dmxNumInputs)
        dmxLog(dmxInfo, "InitOutput: no inputs found\n");

    /* Disable lazy window creation optimization if offscreen
     * optimization is disabled */
    if (!dmxOffScreenOpt && dmxLazyWindowCreation) {
        dmxLog(dmxInfo,
               "InitOutput: Disabling lazy window creation optimization\n");
        dmxLog(dmxInfo,
               "            since it requires the offscreen optimization\n");
        dmxLog(dmxInfo, "            to function properly.\n");
        dmxLazyWindowCreation = FALSE;
    }

    /* Open each display and gather information about it. */
    for (i = 0; i < dmxNumScreens; i++)
        dmxDisplayInit(&dmxScreens[i]);

#if PANORAMIX
    /* Register a Xinerama callback which will run from within
     * PanoramiXCreateConnectionBlock.  We can use the callback to
     * determine if Xinerama is loaded and to check the visuals
     * determined by PanoramiXConsolidate. */
    XineramaRegisterConnectionBlockCallback(dmxConnectionBlockCallback);
#endif

    /* Since we only have a single screen thus far, we only need to set
       the pixmap formats to match that screen.  FIXME: this isn't true. */
    if (!dmxSetPixmapFormats(pScreenInfo, &dmxScreens[0]))
        return;

    /* Might want to install a signal handler to allow cleaning up after
     * unexpected signals.  The DIX/OS layer already handles SIGINT and
     * SIGTERM, so everything is OK for expected signals. --DD
     *
     * SIGHUP, SIGINT, and SIGTERM are trapped in os/connection.c
     * SIGQUIT is another common signal that is sent from the keyboard.
     * Trap it here, to ensure that the keyboard modifier map and other
     * state for the input devices are restored. (This makes the
     * behavior of SIGQUIT somewhat unexpected, since it will be the
     * same as the behavior of SIGINT.  However, leaving the modifier
     * map of the input devices empty is even more unexpected.) --RF
     */
    OsSignal(SIGQUIT, GiveUp);

#ifdef GLXEXT
    /* Check if GLX extension exists on all back-end servers */
    for (i = 0; i < dmxNumScreens; i++)
        glxSupported &= (dmxScreens[i].glxMajorOpcode > 0);
#endif

    if (serverGeneration == 1)
        dmxAddExtensions(glxSupported);

    /* Tell dix layer about the backend displays */
    for (i = 0; i < dmxNumScreens; i++) {

#ifdef GLXEXT
        if (glxSupported) {
            /*
             * Builds GLX configurations from the list of visuals
             * supported by the back-end server, and give that
             * configuration list to the glx layer - so that he will
             * build the visuals accordingly.
             */

            DMXScreenInfo *dmxScreen = &dmxScreens[i];
            __GLXvisualConfig *configs = NULL;
            dmxGlxVisualPrivate **configprivs = NULL;
            int nconfigs = 0;
            int (*oldErrorHandler) (Display *, XErrorEvent *);

            /* Catch errors if when using an older GLX w/o FBconfigs */
            oldErrorHandler = XSetErrorHandler(dmxNOPErrorHandler);

            /* Get FBConfigs of the back-end server */
            dmxScreen->fbconfigs = GetGLXFBConfigs(dmxScreen->beDisplay,
                                                   dmxScreen->glxMajorOpcode,
                                                   &dmxScreen->numFBConfigs);

            XSetErrorHandler(oldErrorHandler);

            dmxScreen->glxVisuals =
                GetGLXVisualConfigs(dmxScreen->beDisplay,
                                    DefaultScreen(dmxScreen->beDisplay),
                                    &dmxScreen->numGlxVisuals);

            if (dmxScreen->fbconfigs) {
                configs =
                    GetGLXVisualConfigsFromFBConfigs(dmxScreen->fbconfigs,
                                                     dmxScreen->numFBConfigs,
                                                     dmxScreen->beVisuals,
                                                     dmxScreen->beNumVisuals,
                                                     dmxScreen->glxVisuals,
                                                     dmxScreen->numGlxVisuals,
                                                     &nconfigs);
            }
            else {
                configs = dmxScreen->glxVisuals;
                nconfigs = dmxScreen->numGlxVisuals;
            }

            configprivs = malloc(nconfigs * sizeof(dmxGlxVisualPrivate *));

            if (configs != NULL && configprivs != NULL) {
                int j;

                /* Initialize our private info for each visual
                 * (currently only x_visual_depth and x_visual_class)
                 */
                for (j = 0; j < nconfigs; j++) {

                    configprivs[j] = (dmxGlxVisualPrivate *)
                        malloc(sizeof(dmxGlxVisualPrivate));
                    configprivs[j]->x_visual_depth = 0;
                    configprivs[j]->x_visual_class = 0;

                    /* Find the visual depth */
                    if (configs[j].vid > 0) {
                        int k;

                        for (k = 0; k < dmxScreen->beNumVisuals; k++) {
                            if (dmxScreen->beVisuals[k].visualid ==
                                configs[j].vid) {
                                configprivs[j]->x_visual_depth =
                                    dmxScreen->beVisuals[k].depth;
                                configprivs[j]->x_visual_class =
                                    dmxScreen->beVisuals[k].class;
                                break;
                            }
                        }
                    }
                }

                XFlush(dmxScreen->beDisplay);
            }
        }
#endif                          /* GLXEXT */

        AddScreen(dmxScreenInit, argc, argv);
    }

    /* Compute origin information. */
    dmxInitOrigins();

    /* Compute overlap information. */
    dmxInitOverlap();

    /* Make sure there is a global width/height available */
    dmxComputeWidthHeight(DMX_NO_RECOMPUTE_BOUNDING_BOX);

    /* FIXME: The following is temporarily placed here.  When the DMX
     * extension is available, it will be move there.
     */
    dmxInitFonts();

    /* Initialize the render extension */
    if (!noRenderExtension)
        dmxInitRender();

    /* Initialized things that need timer hooks */
    dmxStatInit();
    dmxSyncInit();              /* Calls RegisterBlockAndWakeupHandlers */
}

/* RATS: Assuming the fp string (which comes from the command-line argv
         vector) is NULL-terminated, the buffer is large enough for the
         strcpy. */
static void
dmxSetDefaultFontPath(const char *fp)
{
    if (dmxFontPath) {
        int fplen = strlen(fp) + 1;
        int len = strlen(dmxFontPath);

        dmxFontPath = realloc(dmxFontPath, len + fplen + 1);
        dmxFontPath[len] = ',';
        strncpy(&dmxFontPath[len + 1], fp, fplen);
    }
    else {
        dmxFontPath = strdup(fp);
    }

    defaultFontPath = dmxFontPath;
}

/** This function is called in Xserver/os/utils.c from \a AbortServer().
 * We must ensure that backend and console state is restored in the
 * event the server shutdown wasn't clean. */
void
AbortDDX(enum ExitCode error)
{
    int i;

    for (i = 0; i < dmxNumScreens; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];

        if (dmxScreen->beDisplay)
            XCloseDisplay(dmxScreen->beDisplay);
        dmxScreen->beDisplay = NULL;
    }
}

#ifdef DDXBEFORERESET
void
ddxBeforeReset(void)
{
}
#endif

/** This function is called in Xserver/dix/main.c from \a main() when
 * dispatchException & DE_TERMINATE (which is the only way to exit the
 * main loop without an interruption. */
void
ddxGiveUp(enum ExitCode error)
{
    AbortDDX(error);
}

/** This function is called in Xserver/os/osinit.c from \a OsInit(). */
void
OsVendorInit(void)
{
}

/** This function is called in Xserver/os/utils.c from \a FatalError()
 * and \a VFatalError().  (Note that setting the function pointer \a
 * OsVendorVErrorFProc will cause \a VErrorF() (which is called by the
 * two routines mentioned here, as well as by others) to use the
 * referenced routine instead of \a vfprintf().) */
void
OsVendorFatalError(const char *f, va_list args)
{
}

/** Process our command line arguments. */
int
ddxProcessArgument(int argc, char *argv[], int i)
{
    int retval = 0;

    if (!strcmp(argv[i], "-display")) {
        if (++i < argc)
            dmxConfigStoreDisplay(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-inputfrom") || !strcmp(argv[i], "-input")) {
        if (++i < argc)
            dmxConfigStoreInput(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-xinputfrom") || !strcmp(argv[i], "-xinput")) {
        if (++i < argc)
            dmxConfigStoreXInput(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-noshadowfb")) {
        retval = 1;
    }
    else if (!strcmp(argv[i], "-nomulticursor")) {
        dmxCursorNoMulti();
        retval = 1;
    }
    else if (!strcmp(argv[i], "-shadowfb")) {
        retval = 1;
    }
    else if (!strcmp(argv[i], "-configfile")) {
        if (++i < argc)
            dmxConfigStoreFile(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-config")) {
        if (++i < argc)
            dmxConfigStoreConfig(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-fontpath")) {
        if (++i < argc)
            dmxSetDefaultFontPath(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-stat")) {
        if ((i += 2) < argc)
            dmxStatActivate(argv[i - 1], argv[i]);
        retval = 3;
    }
    else if (!strcmp(argv[i], "-syncbatch")) {
        if (++i < argc)
            dmxSyncActivate(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-nooffscreenopt")) {
        dmxOffScreenOpt = FALSE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-nosubdivprims")) {
        dmxSubdividePrimitives = FALSE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-nowindowopt")) {
        dmxLazyWindowCreation = FALSE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-noxkb")) {
        dmxUseXKB = FALSE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-depth")) {
        if (++i < argc)
            dmxDepth = atoi(argv[i]);
        retval = 2;
    }
    else if (!strcmp(argv[i], "-norender")) {
        noRenderExtension = TRUE;
        retval = 1;
#ifdef GLXEXT
    }
    else if (!strcmp(argv[i], "-noglxproxy")) {
        dmxGLXProxy = FALSE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-noglxswapgroup")) {
        dmxGLXSwapGroupSupport = FALSE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-glxsyncswap")) {
        dmxGLXSyncSwap = TRUE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-glxfinishswap")) {
        dmxGLXFinishSwap = TRUE;
        retval = 1;
#endif
    }
    else if (!strcmp(argv[i], "-ignorebadfontpaths")) {
        dmxIgnoreBadFontPaths = TRUE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-addremovescreens")) {
        dmxAddRemoveScreens = TRUE;
        retval = 1;
    }
    else if (!strcmp(argv[i], "-param")) {
        if ((i += 2) < argc) {
            if (!strcasecmp(argv[i - 1], "xkbrules"))
                dmxConfigSetXkbRules(argv[i]);
            else if (!strcasecmp(argv[i - 1], "xkbmodel"))
                dmxConfigSetXkbModel(argv[i]);
            else if (!strcasecmp(argv[i - 1], "xkblayout"))
                dmxConfigSetXkbLayout(argv[i]);
            else if (!strcasecmp(argv[i - 1], "xkbvariant"))
                dmxConfigSetXkbVariant(argv[i]);
            else if (!strcasecmp(argv[i - 1], "xkboptions"))
                dmxConfigSetXkbOptions(argv[i]);
            else
                dmxLog(dmxWarning,
                       "-param requires: XkbRules, XkbModel, XkbLayout,"
                       " XkbVariant, or XkbOptions\n");
        }
        retval = 3;
    }
    if (!serverGeneration)
        dmxConfigSetMaxScreens();
    return retval;
}

/** Provide succinct usage information for the DMX server. */
void
ddxUseMsg(void)
{
    ErrorF("\n\nDevice Dependent Usage:\n");
    ErrorF("-display string      Specify the back-end display(s)\n");
    ErrorF("-input string        Specify input source for core device\n");
    ErrorF("-xinput string       Specify input source for XInput device\n");
    ErrorF("-shadowfb            Enable shadow frame buffer\n");
    ErrorF("-configfile file     Read from a configuration file\n");
    ErrorF("-config config       Select a specific configuration\n");
    ErrorF("-nomulticursor       Turn of multiple cursor support\n");
    ErrorF("-fontpath            Sets the default font path\n");
    ErrorF("-stat inter scrns    Print out performance statistics\n");
    ErrorF("-syncbatch inter     Set interval for XSync batching\n");
    ErrorF("-nooffscreenopt      Disable offscreen optimization\n");
    ErrorF("-nosubdivprims       Disable primitive subdivision\n");
    ErrorF("                     optimization\n");
    ErrorF("-nowindowopt         Disable lazy window creation optimization\n");
    ErrorF("-noxkb               Disable use of the XKB extension with\n");
    ErrorF("                     backend displays (cf. -kb).\n");
    ErrorF("-depth               Specify the default root window depth\n");
    ErrorF("-norender            Disable RENDER extension support\n");
#ifdef GLXEXT
    ErrorF("-noglxproxy          Disable GLX Proxy\n");
    ErrorF("-noglxswapgroup      Disable swap group and swap barrier\n");
    ErrorF("                     extensions in GLX proxy\n");
    ErrorF("-glxsyncswap         Force XSync after swap buffers\n");
    ErrorF("-glxfinishswap       Force glFinish after swap buffers\n");
#endif
    ErrorF
        ("-ignorebadfontpaths  Ignore bad font paths during initialization\n");
    ErrorF("-addremovescreens    Enable dynamic screen addition/removal\n");
    ErrorF("-param ...           Specify configuration parameters (e.g.,\n");
    ErrorF("                     XkbRules, XkbModel, XkbLayout, etc.)\n");
    ErrorF("\n");
    ErrorF("    If the -input string matches a -display string, then input\n"
           "    is taken from that backend display.  (XInput cannot be taken\n"
           "    from a backend display.)  Placing \",console\" after the\n"
           "    display name will force a console window to be opened on\n"
           "    that display in addition to the backend input.  This is\n"
           "    useful if the backend window does not cover the whole\n"
           "    physical display.\n\n");

    ErrorF("    Otherwise, if the -input or -xinput string specifies another\n"
           "    X display, then a console window will be created on that\n"
           "    display.  Placing \",windows\" or \",nowindows\" after the\n"
           "    display name will control the display of window outlines in\n"
           "    the console.\n\n");

    ErrorF("    -input or -xinput dummy specifies no input.\n");
    ErrorF("    -input or -xinput local specifies the use of a raw keyboard,\n"
           "    mouse, or other (extension) device:\n"
           "        -input local,kbd,ps2 will use a ps2 mouse\n"
           "        -input local,kbd,ms  will use a serial mouse\n"
           "        -input local,usb-kbd,usb-mou will use USB devices \n"
           "        -xinput local,usb-oth will use a non-mouse and\n"
           "                non-keyboard USB device with XInput\n\n");

    ErrorF("    Special Keys:\n");
    ErrorF("        Ctrl-Alt-g    Server grab/ungrab (console only)\n");
    ErrorF("        Ctrl-Alt-f    Fine (1-pixel) mouse mode (console only)\n");
    ErrorF("        Ctrl-Alt-q    Quit (core devices only)\n");
    ErrorF("        Ctrl-Alt-F*   Switch to VC (local only)\n");
}
