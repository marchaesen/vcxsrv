
/* Prototypes for DGA functions that the DDX must provide */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifndef _VIDMODEPROC_H_
#define _VIDMODEPROC_H_

typedef enum {
    VIDMODE_H_DISPLAY,
    VIDMODE_H_SYNCSTART,
    VIDMODE_H_SYNCEND,
    VIDMODE_H_TOTAL,
    VIDMODE_H_SKEW,
    VIDMODE_V_DISPLAY,
    VIDMODE_V_SYNCSTART,
    VIDMODE_V_SYNCEND,
    VIDMODE_V_TOTAL,
    VIDMODE_FLAGS,
    VIDMODE_CLOCK
} VidModeSelectMode;

typedef enum {
    VIDMODE_MON_VENDOR,
    VIDMODE_MON_MODEL,
    VIDMODE_MON_NHSYNC,
    VIDMODE_MON_NVREFRESH,
    VIDMODE_MON_HSYNC_LO,
    VIDMODE_MON_HSYNC_HI,
    VIDMODE_MON_VREFRESH_LO,
    VIDMODE_MON_VREFRESH_HI
} VidModeSelectMonitor;

typedef union {
    pointer ptr;
    int i;
    float f;
} vidMonitorValue;

extern Bool VidModeExtensionInit(ScreenPtr pScreen);

extern _X_EXPORT Bool VidModeAvailable(int scrnIndex);
extern _X_EXPORT Bool VidModeGetCurrentModeline(int scrnIndex, pointer *mode,
                                                int *dotClock);
extern _X_EXPORT Bool VidModeGetFirstModeline(int scrnIndex, pointer *mode,
                                              int *dotClock);
extern _X_EXPORT Bool VidModeGetNextModeline(int scrnIndex, pointer *mode,
                                             int *dotClock);
extern _X_EXPORT Bool VidModeDeleteModeline(int scrnIndex, pointer mode);
extern _X_EXPORT Bool VidModeZoomViewport(int scrnIndex, int zoom);
extern _X_EXPORT Bool VidModeGetViewPort(int scrnIndex, int *x, int *y);
extern _X_EXPORT Bool VidModeSetViewPort(int scrnIndex, int x, int y);
extern _X_EXPORT Bool VidModeSwitchMode(int scrnIndex, pointer mode);
extern _X_EXPORT Bool VidModeLockZoom(int scrnIndex, Bool lock);
extern _X_EXPORT Bool VidModeGetMonitor(int scrnIndex, pointer *monitor);
extern _X_EXPORT int VidModeGetNumOfClocks(int scrnIndex, Bool *progClock);
extern _X_EXPORT Bool VidModeGetClocks(int scrnIndex, int *Clocks);
extern _X_EXPORT ModeStatus VidModeCheckModeForMonitor(int scrnIndex,
                                                       pointer mode);
extern _X_EXPORT ModeStatus VidModeCheckModeForDriver(int scrnIndex,
                                                      pointer mode);
extern _X_EXPORT void VidModeSetCrtcForMode(int scrnIndex, pointer mode);
extern _X_EXPORT Bool VidModeAddModeline(int scrnIndex, pointer mode);
extern _X_EXPORT int VidModeGetDotClock(int scrnIndex, int Clock);
extern _X_EXPORT int VidModeGetNumOfModes(int scrnIndex);
extern _X_EXPORT Bool VidModeSetGamma(int scrnIndex, float red, float green,
                                      float blue);
extern _X_EXPORT Bool VidModeGetGamma(int scrnIndex, float *red, float *green,
                                      float *blue);
extern _X_EXPORT pointer VidModeCreateMode(void);
extern _X_EXPORT void VidModeCopyMode(pointer modefrom, pointer modeto);
extern _X_EXPORT int VidModeGetModeValue(pointer mode, int valtyp);
extern _X_EXPORT void VidModeSetModeValue(pointer mode, int valtyp, int val);
extern _X_EXPORT vidMonitorValue VidModeGetMonitorValue(pointer monitor,
                                                        int valtyp, int indx);
extern _X_EXPORT Bool VidModeSetGammaRamp(int, int, CARD16 *, CARD16 *,
                                          CARD16 *);
extern _X_EXPORT Bool VidModeGetGammaRamp(int, int, CARD16 *, CARD16 *,
                                          CARD16 *);
extern _X_EXPORT int VidModeGetGammaRampSize(int scrnIndex);

#endif
