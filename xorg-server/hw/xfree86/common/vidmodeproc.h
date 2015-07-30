
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
    const void *ptr;
    int i;
    float f;
} vidMonitorValue;

extern Bool VidModeExtensionInit(ScreenPtr pScreen);

extern Bool VidModeGetCurrentModeline(int scrnIndex, void **mode,
                                      int *dotClock);
extern Bool VidModeGetFirstModeline(int scrnIndex, void **mode,
                                    int *dotClock);
extern Bool VidModeGetNextModeline(int scrnIndex, void **mode,
                                   int *dotClock);
extern Bool VidModeDeleteModeline(int scrnIndex, void *mode);
extern Bool VidModeZoomViewport(int scrnIndex, int zoom);
extern Bool VidModeGetViewPort(int scrnIndex, int *x, int *y);
extern Bool VidModeSetViewPort(int scrnIndex, int x, int y);
extern Bool VidModeSwitchMode(int scrnIndex, void *mode);
extern Bool VidModeLockZoom(int scrnIndex, Bool lock);
extern Bool VidModeGetMonitor(int scrnIndex, void **monitor);
extern int VidModeGetNumOfClocks(int scrnIndex, Bool *progClock);
extern Bool VidModeGetClocks(int scrnIndex, int *Clocks);
extern ModeStatus VidModeCheckModeForMonitor(int scrnIndex,
                                             void *mode);
extern ModeStatus VidModeCheckModeForDriver(int scrnIndex,
                                            void *mode);
extern void VidModeSetCrtcForMode(int scrnIndex, void *mode);
extern Bool VidModeAddModeline(int scrnIndex, void *mode);
extern int VidModeGetDotClock(int scrnIndex, int Clock);
extern int VidModeGetNumOfModes(int scrnIndex);
extern Bool VidModeSetGamma(int scrnIndex, float red, float green,
                            float blue);
extern Bool VidModeGetGamma(int scrnIndex, float *red, float *green,
                            float *blue);
extern void *VidModeCreateMode(void);
extern void VidModeCopyMode(void *modefrom, void *modeto);
extern int VidModeGetModeValue(void *mode, int valtyp);
extern void VidModeSetModeValue(void *mode, int valtyp, int val);
extern vidMonitorValue VidModeGetMonitorValue(void *monitor,
                                              int valtyp, int indx);
extern Bool VidModeSetGammaRamp(int, int, CARD16 *, CARD16 *,
                                CARD16 *);
extern Bool VidModeGetGammaRamp(int, int, CARD16 *, CARD16 *,
                                CARD16 *);
extern int VidModeGetGammaRampSize(int scrnIndex);

#endif
