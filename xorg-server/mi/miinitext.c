/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#undef XV
#undef DBE
#undef XF86VIDMODE
#undef XFreeXDGA
#undef XF86DRI
#undef SCREENSAVER
#undef RANDR
#undef XFIXES
#undef DAMAGE
#undef COMPOSITE
#undef MITSHM
#endif

#ifdef HAVE_XNEST_CONFIG_H
#include <xnest-config.h>
#undef COMPOSITE
#undef DPMSExtension
#endif

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
/* there must be a better way... */
#undef XFreeXDGA
#undef XF86DRI
#undef XF86VIDMODE
#endif

#ifdef HAVE_XGL_CONFIG_H
#include <xgl-config.h>
#undef XFreeXDGA
#undef XF86DRI
#undef XF86VIDMODE
#endif

#include "misc.h"
#include "extension.h"
#include "micmap.h"
#include "globals.h"

extern Bool noTestExtensions;

#ifdef COMPOSITE
extern Bool noCompositeExtension;
#endif
#ifdef DBE
extern Bool noDbeExtension;
#endif
#ifdef DPMSExtension
extern Bool noDPMSExtension;
#endif
#ifdef GLXEXT
extern Bool noGlxExtension;
#endif
#ifdef SCREENSAVER
extern Bool noScreenSaverExtension;
#endif
#ifdef MITSHM
extern Bool noMITShmExtension;
#endif
#ifdef RANDR
extern Bool noRRExtension;
#endif
extern Bool noRenderExtension;

#ifdef XCSECURITY
extern Bool noSecurityExtension;
#endif
#ifdef RES
extern Bool noResExtension;
#endif
#ifdef XF86BIGFONT
extern Bool noXFree86BigfontExtension;
#endif
#ifdef XFreeXDGA
extern Bool noXFree86DGAExtension;
#endif
#ifdef XF86DRI
extern Bool noXFree86DRIExtension;
#endif
#ifdef XF86VIDMODE
extern Bool noXFree86VidModeExtension;
#endif
#ifdef XFIXES
extern Bool noXFixesExtension;
#endif
#ifdef PANORAMIX
extern Bool noPanoramiXExtension;
#endif
#ifdef INXQUARTZ
extern Bool noPseudoramiXExtension;
#endif
#ifdef XSELINUX
extern Bool noSELinuxExtension;
#endif
#ifdef XV
extern Bool noXvExtension;
#endif
extern Bool noGEExtension;

#ifndef XFree86LOADER
#define INITARGS void
typedef void (*InitExtension) (INITARGS);
#else                           /* XFree86Loader */
#include "loaderProcs.h"
#endif

#ifdef MITSHM
#include <X11/extensions/shm.h>
#endif
#ifdef XTEST
#include <X11/extensions/xtestconst.h>
#endif
#include <X11/extensions/XKB.h>
#ifdef XCSECURITY
#include "securitysrv.h"
#include <X11/extensions/secur.h>
#endif
#ifdef XSELINUX
#include "xselinux.h"
#endif
#ifdef PANORAMIX
#include <X11/extensions/panoramiXproto.h>
#endif
#ifdef XF86BIGFONT
#include <X11/extensions/xf86bigfproto.h>
#endif
#ifdef RES
#include <X11/extensions/XResproto.h>
#endif

/* FIXME: this whole block of externs should be from the appropriate headers */
#ifdef MITSHM
extern void ShmExtensionInit(INITARGS);
#endif
#ifdef PANORAMIX
extern void PanoramiXExtensionInit(INITARGS);
#endif
#ifdef INXQUARTZ
extern void PseudoramiXExtensionInit(INITARGS);
#endif
extern void XInputExtensionInit(INITARGS);

#ifdef XTEST
extern void XTestExtensionInit(INITARGS);
#endif
extern void BigReqExtensionInit(INITARGS);

#ifdef SCREENSAVER
extern void ScreenSaverExtensionInit(INITARGS);
#endif
#ifdef XV
extern void XvExtensionInit(INITARGS);
extern void XvMCExtensionInit(INITARGS);
#endif
extern void SyncExtensionInit(INITARGS);
extern void XkbExtensionInit(INITARGS);
extern void XCMiscExtensionInit(INITARGS);

#ifdef XRECORD
extern void RecordExtensionInit(INITARGS);
#endif
#ifdef DBE
extern void DbeExtensionInit(INITARGS);
#endif
#ifdef XCSECURITY
extern void SecurityExtensionInit(INITARGS);
#endif
#ifdef XSELINUX
extern void SELinuxExtensionInit(INITARGS);
#endif
#ifdef XF86BIGFONT
extern void XFree86BigfontExtensionInit(INITARGS);
#endif
#ifdef XF86VIDMODE
extern void XFree86VidModeExtensionInit(INITARGS);
#endif
#ifdef XFreeXDGA
extern void XFree86DGAExtensionInit(INITARGS);
#endif
#ifdef GLXEXT
typedef struct __GLXprovider __GLXprovider;
extern __GLXprovider __glXDRISWRastProvider;
extern void GlxPushProvider(__GLXprovider * impl);
extern void GlxExtensionInit(INITARGS);
#endif
#ifdef XF86DRI
extern void XFree86DRIExtensionInit(INITARGS);
#endif
#ifdef DPMSExtension
extern void DPMSExtensionInit(INITARGS);
#endif
extern void RenderExtensionInit(INITARGS);

#ifdef RANDR
extern void RRExtensionInit(INITARGS);
#endif
#ifdef RES
extern void ResExtensionInit(INITARGS);
#endif
#ifdef DMXEXT
extern void DMXExtensionInit(INITARGS);
#endif
#ifdef XFIXES
extern void XFixesExtensionInit(INITARGS);
#endif
#ifdef DAMAGE
extern void DamageExtensionInit(INITARGS);
#endif
#ifdef COMPOSITE
extern void CompositeExtensionInit(INITARGS);
#endif
extern void GEExtensionInit(INITARGS);

/* The following is only a small first step towards run-time
 * configurable extensions.
 */
typedef struct {
    const char *name;
    Bool *disablePtr;
} ExtensionToggle;

static ExtensionToggle ExtensionToggleList[] = {
    /* sort order is extension name string as shown in xdpyinfo */
    {"Generic Events", &noGEExtension},
#ifdef COMPOSITE
    {"Composite", &noCompositeExtension},
#endif
#ifdef DAMAGE
    {"DAMAGE", &noDamageExtension},
#endif
#ifdef DBE
    {"DOUBLE-BUFFER", &noDbeExtension},
#endif
#ifdef DPMSExtension
    {"DPMS", &noDPMSExtension},
#endif
#ifdef GLXEXT
    {"GLX", &noGlxExtension},
#endif
#ifdef SCREENSAVER
    {"MIT-SCREEN-SAVER", &noScreenSaverExtension},
#endif
#ifdef MITSHM
    {SHMNAME, &noMITShmExtension},
#endif
#ifdef RANDR
    {"RANDR", &noRRExtension},
#endif
    {"RENDER", &noRenderExtension},
#ifdef XCSECURITY
    {"SECURITY", &noSecurityExtension},
#endif
#ifdef RES
    {"X-Resource", &noResExtension},
#endif
#ifdef XF86BIGFONT
    {"XFree86-Bigfont", &noXFree86BigfontExtension},
#endif
#ifdef XFreeXDGA
    {"XFree86-DGA", &noXFree86DGAExtension},
#endif
#ifdef XF86DRI
    {"XFree86-DRI", &noXFree86DRIExtension},
#endif
#ifdef XF86VIDMODE
    {"XFree86-VidModeExtension", &noXFree86VidModeExtension},
#endif
#ifdef XFIXES
    {"XFIXES", &noXFixesExtension},
#endif
#ifdef PANORAMIX
    {"XINERAMA", &noPanoramiXExtension},
#endif
    {"XInputExtension", NULL},
    {"XKEYBOARD", NULL},
#ifdef XSELINUX
    {"SELinux", &noSELinuxExtension},
#endif
    {"XTEST", &noTestExtensions},
#ifdef XV
    {"XVideo", &noXvExtension},
#endif
    {NULL, NULL}
};

Bool
EnableDisableExtension(const char *name, Bool enable)
{
    ExtensionToggle *ext = &ExtensionToggleList[0];

    for (ext = &ExtensionToggleList[0]; ext->name != NULL; ext++) {
        if (strcmp(name, ext->name) == 0) {
            if (ext->disablePtr != NULL) {
                *ext->disablePtr = !enable;
                return TRUE;
            }
            else {
                /* Extension is always on, impossible to disable */
                return enable;  /* okay if they wanted to enable,
                                   fail if they tried to disable */
            }
        }
    }

    return FALSE;
}

void
EnableDisableExtensionError(const char *name, Bool enable)
{
    ExtensionToggle *ext = &ExtensionToggleList[0];
    Bool found = FALSE;

    for (ext = &ExtensionToggleList[0]; ext->name != NULL; ext++) {
        if ((strcmp(name, ext->name) == 0) && (ext->disablePtr == NULL)) {
            ErrorF("[mi] Extension \"%s\" can not be disabled\n", name);
            found = TRUE;
            break;
        }
    }
    if (found == FALSE)
        ErrorF("[mi] Extension \"%s\" is not recognized\n", name);
    ErrorF("[mi] Only the following extensions can be run-time %s:\n",
           enable ? "enabled" : "disabled");
    for (ext = &ExtensionToggleList[0]; ext->name != NULL; ext++) {
        if (ext->disablePtr != NULL) {
            ErrorF("[mi]    %s\n", ext->name);
        }
    }
}

#ifndef XFree86LOADER

 /*ARGSUSED*/ void
InitExtensions(int argc, char *argv[])
{
    if (!noGEExtension)
        GEExtensionInit();

#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        PanoramiXExtensionInit();
#endif
#ifdef INXQUARTZ
    if (!noPseudoramiXExtension)
        PseudoramiXExtensionInit();
#endif
    ShapeExtensionInit();
#ifdef MITSHM
    if (!noMITShmExtension)
        ShmExtensionInit();
#endif
    XInputExtensionInit();
#ifdef XTEST
    if (!noTestExtensions)
        XTestExtensionInit();
#endif
    BigReqExtensionInit();
#if defined(SCREENSAVER)
    if (!noScreenSaverExtension)
        ScreenSaverExtensionInit();
#endif
#ifdef XV
    if (!noXvExtension) {
        XvExtensionInit();
        XvMCExtensionInit();
    }
#endif
    SyncExtensionInit();
    XkbExtensionInit();
    XCMiscExtensionInit();
#ifdef XRECORD
    if (!noTestExtensions)
        RecordExtensionInit();
#endif
#ifdef DBE
    if (!noDbeExtension)
        DbeExtensionInit();
#endif
#ifdef XCSECURITY
    if (!noSecurityExtension)
        SecurityExtensionInit();
#endif
#ifdef XSELINUX
    if (!noSELinuxExtension)
        SELinuxExtensionInit();
#endif
#if defined(DPMSExtension) && !defined(NO_HW_ONLY_EXTS)
    if (!noDPMSExtension)
        DPMSExtensionInit();
#endif
#ifdef XF86BIGFONT
    if (!noXFree86BigfontExtension)
        XFree86BigfontExtensionInit();
#endif
#if !defined(NO_HW_ONLY_EXTS)
#if defined(XF86VIDMODE)
    if (!noXFree86VidModeExtension)
        XFree86VidModeExtensionInit();
#endif
#if defined(XFreeXDGA)
    if (!noXFree86DGAExtension)
        XFree86DGAExtensionInit();
#endif
#ifdef XF86DRI
    if (!noXFree86DRIExtension)
        XFree86DRIExtensionInit();
#endif
#endif
#ifdef XFIXES
    /* must be before Render to layer DisplayCursor correctly */
    if (!noXFixesExtension)
        XFixesExtensionInit();
#endif
    if (!noRenderExtension)
        RenderExtensionInit();
#ifdef RANDR
    if (!noRRExtension)
        RRExtensionInit();
#endif
#ifdef RES
    if (!noResExtension)
        ResExtensionInit();
#endif
#ifdef DMXEXT
    DMXExtensionInit();         /* server-specific extension, cannot be disabled */
#endif
#ifdef COMPOSITE
    if (!noCompositeExtension)
        CompositeExtensionInit();
#endif
#ifdef DAMAGE
    if (!noDamageExtension)
        DamageExtensionInit();
#endif

#ifdef GLXEXT
    if (serverGeneration == 1)
        GlxPushProvider(&__glXDRISWRastProvider);
    if (!noGlxExtension)
        GlxExtensionInit();
#endif
}

#else                           /* XFree86LOADER */
/* List of built-in (statically linked) extensions */
static ExtensionModule staticExtensions[] = {
    {GEExtensionInit, "Generic Event Extension", &noGEExtension, NULL, NULL},
    {ShapeExtensionInit, "SHAPE", NULL, NULL, NULL},
#ifdef MITSHM
    {ShmExtensionInit, SHMNAME, &noMITShmExtension, NULL, NULL},
#endif
    {XInputExtensionInit, "XInputExtension", NULL, NULL, NULL},
#ifdef XTEST
    {XTestExtensionInit, XTestExtensionName, &noTestExtensions, NULL, NULL},
#endif
    {BigReqExtensionInit, "BIG-REQUESTS", NULL, NULL, NULL},
    {SyncExtensionInit, "SYNC", NULL, NULL, NULL},
    {XkbExtensionInit, XkbName, NULL, NULL, NULL},
    {XCMiscExtensionInit, "XC-MISC", NULL, NULL, NULL},
#ifdef XCSECURITY
    {SecurityExtensionInit, SECURITY_EXTENSION_NAME, &noSecurityExtension, NULL,
     NULL},
#endif
#ifdef PANORAMIX
    {PanoramiXExtensionInit, PANORAMIX_PROTOCOL_NAME, &noPanoramiXExtension,
     NULL, NULL},
#endif
#ifdef XFIXES
    /* must be before Render to layer DisplayCursor correctly */
    {XFixesExtensionInit, "XFIXES", &noXFixesExtension, NULL, NULL},
#endif
#ifdef XF86BIGFONT
    {XFree86BigfontExtensionInit, XF86BIGFONTNAME, &noXFree86BigfontExtension,
     NULL, NULL},
#endif
    {RenderExtensionInit, "RENDER", &noRenderExtension, NULL, NULL},
#ifdef RANDR
    {RRExtensionInit, "RANDR", &noRRExtension, NULL, NULL},
#endif
#ifdef COMPOSITE
    {CompositeExtensionInit, "COMPOSITE", &noCompositeExtension, NULL},
#endif
#ifdef DAMAGE
    {DamageExtensionInit, "DAMAGE", &noDamageExtension, NULL},
#endif
    {NULL, NULL, NULL, NULL, NULL}
};

 /*ARGSUSED*/ void
InitExtensions(int argc, char *argv[])
{
    int i;
    ExtensionModule *ext;
    static Bool listInitialised = FALSE;

    if (!listInitialised) {
        /* Add built-in extensions to the list. */
        for (i = 0; staticExtensions[i].name; i++)
            LoadExtension(&staticExtensions[i], TRUE);

        /* Sort the extensions according the init dependencies. */
        LoaderSortExtensions();
        listInitialised = TRUE;
    }

    for (i = 0; ExtensionModuleList[i].name != NULL; i++) {
        ext = &ExtensionModuleList[i];
        if (ext->initFunc != NULL &&
            (ext->disablePtr == NULL || !*ext->disablePtr)) {
            (ext->initFunc) ();
        }
    }
}

#endif                          /* XFree86LOADER */
