
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/extensions/shapeproto.h>

#ifdef XTEST
extern void XTestExtensionInit(void);
#include <X11/extensions/xtestproto.h>
#endif

#if 1
extern void XTestExtension1Init(void);
#endif

#if 1
extern void XCMiscExtensionInit(void);
#endif

#ifdef SCREENSAVER
extern void ScreenSaverExtensionInit(void);
#include <X11/extensions/saver.h>
#endif

#ifdef XF86VIDMODE
extern void XFree86VidModeExtensionInit(void);
#include <X11/extensions/xf86vmproto.h>
#endif

#ifdef XFreeXDGA
extern void XFree86DGAExtensionInit(void);
extern void XFree86DGARegister(void);
#include <X11/extensions/xf86dgaproto.h>
#endif

#ifdef DPMSExtension
extern void DPMSExtensionInit(void);
#include <X11/extensions/dpmsconst.h>
#endif

#ifdef XV
extern void XvExtensionInit(void);
extern void XvMCExtensionInit(void);
extern void XvRegister(void);
#include <X11/extensions/Xv.h>
#include <X11/extensions/XvMC.h>
#endif

#ifdef RES
extern void ResExtensionInit(void);
#include <X11/extensions/XResproto.h>
#endif

#ifdef SHM
#include <X11/extensions/shmproto.h>
extern void ShmExtensionInit(void);
extern void ShmRegisterFuncs(ScreenPtr pScreen, ShmFuncsPtr funcs);
#endif

#ifdef XSELINUX
extern void SELinuxExtensionInit(void);
#include "xselinux.h"
#endif

#ifdef XEVIE
extern void XevieExtensionInit(void);
#endif

#if 1
extern void SecurityExtensionInit(void);
#endif

#if 1
extern void PanoramiXExtensionInit(void);
#endif

#if 1
extern void XkbExtensionInit(void);
#endif
