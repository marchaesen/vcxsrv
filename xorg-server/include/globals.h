
#ifndef _XSERV_GLOBAL_H_
#define _XSERV_GLOBAL_H_

#include "window.h"             /* for WindowPtr */

/* Global X server variables that are visible to mi, dix, os, and ddx */

extern _X_EXPORT CARD32 defaultScreenSaverTime;
extern _X_EXPORT CARD32 defaultScreenSaverInterval;
extern _X_EXPORT CARD32 ScreenSaverTime;
extern _X_EXPORT CARD32 ScreenSaverInterval;

#ifdef SCREENSAVER
extern _X_EXPORT Bool screenSaverSuspended;
#endif

extern _X_EXPORT char *defaultFontPath;
extern _X_EXPORT int monitorResolution;
extern _X_EXPORT int defaultColorVisualClass;

extern _X_EXPORT int GrabInProgress;
extern _X_EXPORT Bool noTestExtensions;
extern _X_EXPORT char *SeatId;
extern _X_EXPORT char *ConnectionInfo;

#ifdef DPMSExtension
extern _X_EXPORT CARD32 DPMSStandbyTime;
extern _X_EXPORT CARD32 DPMSSuspendTime;
extern _X_EXPORT CARD32 DPMSOffTime;
extern _X_EXPORT CARD16 DPMSPowerLevel;
extern _X_EXPORT Bool DPMSEnabled;
extern _X_EXPORT Bool DPMSDisabledSwitch;
extern _X_EXPORT Bool DPMSCapableFlag;
#endif

#ifdef PANORAMIX
extern _X_EXPORT Bool PanoramiXExtensionDisabledHack;
#endif

#ifdef COMPOSITE
extern _X_EXPORT Bool noCompositeExtension;
#endif

#ifdef DAMAGE
extern _X_EXPORT Bool noDamageExtension;
#endif

#ifdef DBE
extern _X_EXPORT Bool noDbeExtension;
#endif

#ifdef DPMSExtension
extern _X_EXPORT Bool noDPMSExtension;
#endif

#ifdef GLXEXT
extern _X_EXPORT Bool noGlxExtension;
#endif

#ifdef SCREENSAVER
extern _X_EXPORT Bool noScreenSaverExtension;
#endif

#ifdef MITSHM
extern _X_EXPORT Bool noMITShmExtension;
#endif

#ifdef RANDR
extern _X_EXPORT Bool noRRExtension;
#endif

extern _X_EXPORT Bool noRenderExtension;

#ifdef XCSECURITY
extern _X_EXPORT Bool noSecurityExtension;
#endif

#ifdef RES
extern _X_EXPORT Bool noResExtension;
#endif

#ifdef XF86BIGFONT
extern _X_EXPORT Bool noXFree86BigfontExtension;
#endif

#ifdef XFreeXDGA
extern _X_EXPORT Bool noXFree86DGAExtension;
#endif

#ifdef XF86DRI
extern _X_EXPORT Bool noXFree86DRIExtension;
#endif

#ifdef XF86VIDMODE
extern _X_EXPORT Bool noXFree86VidModeExtension;
#endif

#ifdef XFIXES
extern _X_EXPORT Bool noXFixesExtension;
#endif

#ifdef PANORAMIX
extern _X_EXPORT Bool noPanoramiXExtension;
#endif

#ifdef XSELINUX
extern _X_EXPORT Bool noSELinuxExtension;

#define SELINUX_MODE_DEFAULT    0
#define SELINUX_MODE_DISABLED   1
#define SELINUX_MODE_PERMISSIVE 2
#define SELINUX_MODE_ENFORCING  3
extern _X_EXPORT int selinuxEnforcingState;
#endif

#ifdef XV
extern _X_EXPORT Bool noXvExtension;
#endif

#ifdef DRI2
extern _X_EXPORT Bool noDRI2Extension;
#endif

#endif                          /* !_XSERV_GLOBAL_H_ */
