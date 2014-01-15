
#ifndef _XSERV_GLOBAL_H_
#define _XSERV_GLOBAL_H_

#include <signal.h>

#include "window.h"             /* for WindowPtr */
#include "extinit.h"

/* Global X server variables that are visible to mi, dix, os, and ddx */

extern _X_EXPORT CARD32 defaultScreenSaverTime;
extern _X_EXPORT CARD32 defaultScreenSaverInterval;
extern _X_EXPORT CARD32 ScreenSaverTime;
extern _X_EXPORT CARD32 ScreenSaverInterval;

#ifdef SCREENSAVER
extern _X_EXPORT Bool screenSaverSuspended;
#endif

extern _X_EXPORT const char *defaultFontPath;
extern _X_EXPORT int monitorResolution;
extern _X_EXPORT int defaultColorVisualClass;

extern _X_EXPORT int GrabInProgress;
extern _X_EXPORT Bool noTestExtensions;
extern _X_EXPORT char *SeatId;
extern _X_EXPORT char *ConnectionInfo;
extern _X_EXPORT sig_atomic_t inSignalContext;

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

#ifdef XSELINUX
#define SELINUX_MODE_DEFAULT    0
#define SELINUX_MODE_DISABLED   1
#define SELINUX_MODE_PERMISSIVE 2
#define SELINUX_MODE_ENFORCING  3
extern _X_EXPORT int selinuxEnforcingState;
#endif

#endif                          /* !_XSERV_GLOBAL_H_ */
