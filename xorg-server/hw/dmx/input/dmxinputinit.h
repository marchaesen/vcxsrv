/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
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
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Interface for low-level input support.  \see dmxinputinit.c */

#ifndef _DMXINPUTINIT_H_
#define _DMXINPUTINIT_H_

#include "dmx.h"
#include "dmxinput.h"
#include "dmxlog.h"

#define DMX_LOCAL_DEFAULT_KEYBOARD "kbd"
#define DMX_LOCAL_DEFAULT_POINTER  "ps2"
#define DMX_MAX_BUTTONS            256
#define DMX_MOTION_SIZE            256
#define DMX_MAX_VALUATORS          32
#define DMX_MAX_AXES               32
#define DMX_MAX_XINPUT_EVENT_TYPES 100
#define DMX_MAP_ENTRIES            16   /* Must be a power of 2 */
#define DMX_MAP_MASK               (DMX_MAP_ENTRIES - 1)

typedef enum {
    DMX_FUNCTION_GRAB,
    DMX_FUNCTION_TERMINATE,
    DMX_FUNCTION_FINE
} DMXFunctionType;

typedef enum {
    DMX_LOCAL_HIGHLEVEL,
    DMX_LOCAL_KEYBOARD,
    DMX_LOCAL_MOUSE,
    DMX_LOCAL_OTHER
} DMXLocalInputType;

typedef enum {
    DMX_LOCAL_TYPE_LOCAL,
    DMX_LOCAL_TYPE_CONSOLE,
    DMX_LOCAL_TYPE_BACKEND,
    DMX_LOCAL_TYPE_COMMON
} DMXLocalInputExtType;

typedef enum {
    DMX_RELATIVE,
    DMX_ABSOLUTE,
    DMX_ABSOLUTE_CONFINED
} DMXMotionType;

/** Stores information from low-level device that is used to initialize
 * the device at the dix level. */
typedef struct _DMXLocalInitInfo {
    int keyboard;                  /**< Non-zero if the device is a keyboard */

    int keyClass;                  /**< Non-zero if keys are present */
    KeySymsRec keySyms;            /**< Key symbols */
    int freemap;                   /**< If non-zero, free keySyms.map */
    CARD8 modMap[MAP_LENGTH];                /**< Modifier map */
    XkbDescPtr xkb;                 /**< XKB description */
    XkbComponentNamesRec names;     /**< XKB component names */
    int freenames;                  /**< Non-zero if names should be free'd */
    int force;                      /**< Do not allow command line override */

    int buttonClass;                  /**< Non-zero if buttons are present */
    int numButtons;                   /**< Number of buttons */
    unsigned char map[DMX_MAX_BUTTONS];        /**< Button map */

    int valuatorClass;                  /**< Non-zero if valuators are
                                         * present */
    int numRelAxes;                     /**< Number of relative axes */
    int numAbsAxes;                     /**< Number of absolute axes */
    int minval[DMX_MAX_AXES];                  /**< Minimum values */
    int maxval[DMX_MAX_AXES];                  /**< Maximum values */
    int res[DMX_MAX_AXES];                     /**< Resolution */
    int minres[DMX_MAX_AXES];                  /**< Minimum resolutions */
    int maxres[DMX_MAX_AXES];                  /**< Maximum resolutions */

    int focusClass;                        /**< Non-zero if device can
                                            * cause focus */
    int proximityClass;                    /**< Non-zero if device
                                            * causes proximity events */
    int kbdFeedbackClass;                  /**< Non-zero if device has
                                            * keyboard feedback */
    int ptrFeedbackClass;                  /**< Non-zero if device has
                                            * pointer feedback */
    int ledFeedbackClass;                  /**< Non-zero if device has
                                            * LED indicators */
    int belFeedbackClass;                  /**< Non-zero if device has a
                                            * bell */
    int intFeedbackClass;                  /**< Non-zero if device has
                                            * integer feedback */
    int strFeedbackClass;                  /**< Non-zero if device has
                                            * string feedback */

    int maxSymbols;                           /**< Maximum symbols */
    int maxSymbolsSupported;                  /**< Maximum symbols supported */
    KeySym *symbols;                          /**< Key symbols */
} DMXLocalInitInfo, *DMXLocalInitInfoPtr;

typedef pointer (*dmxCreatePrivateProcPtr) (DeviceIntPtr);
typedef void (*dmxDestroyPrivateProcPtr) (pointer);

typedef void (*dmxInitProcPtr) (DevicePtr);
typedef void (*dmxReInitProcPtr) (DevicePtr);
typedef void (*dmxLateReInitProcPtr) (DevicePtr);
typedef void (*dmxGetInfoProcPtr) (DevicePtr, DMXLocalInitInfoPtr);
typedef int (*dmxOnProcPtr) (DevicePtr);
typedef void (*dmxOffProcPtr) (DevicePtr);
typedef void (*dmxUpdatePositionProcPtr) (pointer, int x, int y);

typedef void (*dmxVTPreSwitchProcPtr) (pointer);        /* Turn I/O Off */
typedef void (*dmxVTPostSwitchProcPtr) (pointer);       /* Turn I/O On */
typedef void (*dmxVTSwitchReturnProcPtr) (pointer);
typedef int (*dmxVTSwitchProcPtr) (pointer, int vt,
                                   dmxVTSwitchReturnProcPtr, pointer);

typedef void (*dmxMotionProcPtr) (DevicePtr,
                                  int *valuators,
                                  int firstAxis,
                                  int axesCount,
                                  DMXMotionType type, DMXBlockType block);
typedef void (*dmxEnqueueProcPtr) (DevicePtr, int type, int detail,
                                   KeySym keySym, XEvent * e,
                                   DMXBlockType block);
typedef int (*dmxCheckSpecialProcPtr) (DevicePtr, KeySym keySym);
typedef void (*dmxCollectEventsProcPtr) (DevicePtr,
                                         dmxMotionProcPtr,
                                         dmxEnqueueProcPtr,
                                         dmxCheckSpecialProcPtr, DMXBlockType);
typedef void (*dmxProcessInputProcPtr) (pointer);
typedef void (*dmxUpdateInfoProcPtr) (pointer, DMXUpdateType, WindowPtr);
typedef int (*dmxFunctionsProcPtr) (pointer, DMXFunctionType);

typedef void (*dmxKBCtrlProcPtr) (DevicePtr, KeybdCtrl * ctrl);
typedef void (*dmxMCtrlProcPtr) (DevicePtr, PtrCtrl * ctrl);
typedef void (*dmxKBBellProcPtr) (DevicePtr, int percent,
                                  int volume, int pitch, int duration);

/** Stores a mapping between the device id on the remote X server and
 * the id on the DMX server */
typedef struct _DMXEventMap {
    int remote;                 /**< Event number on remote X server */
    int server;                 /**< Event number (unbiased) on DMX server */
} DMXEventMap;

/** This is the device-independent structure used by the low-level input
 * routines.  The contents are not exposed to top-level .c files (except
 * dmxextensions.c).  \see dmxinput.h \see dmxextensions.c */
typedef struct _DMXLocalInputInfo {
    const char *name;                 /**< Device name */
    DMXLocalInputType type;           /**< Device type  */
    DMXLocalInputExtType extType;     /**< Extended device type */
    int binding;                      /**< Count of how many consecutive
                                       * structs are bound to the same
                                       * device */

    /* Low-level (e.g., keyboard/mouse drivers) */

    dmxCreatePrivateProcPtr create_private;   /**< Create
                                               * device-dependent
                                               * private */
    dmxDestroyPrivateProcPtr destroy_private; /**< Destroy
                                               * device-dependent
                                               * private */
    dmxInitProcPtr init;                      /**< Initialize device  */
    dmxReInitProcPtr reinit;                  /**< Reinitialize device
                                               * (during a
                                               * reconfiguration) */
    dmxLateReInitProcPtr latereinit;          /**< Reinitialize a device
                                               * (called very late
                                               * during a
                                               * reconfiguration) */
    dmxGetInfoProcPtr get_info;               /**< Get device information */
    dmxOnProcPtr on;                          /**< Turn device on */
    dmxOffProcPtr off;                        /**< Turn device off */
    dmxUpdatePositionProcPtr update_position; /**< Called when another
                                               * device updates the
                                               * cursor position */
    dmxVTPreSwitchProcPtr vt_pre_switch;      /**< Called before a VT switch */
    dmxVTPostSwitchProcPtr vt_post_switch;    /**< Called after a VT switch */
    dmxVTSwitchProcPtr vt_switch;             /**< Causes a VT switch */

    dmxCollectEventsProcPtr collect_events;   /**< Collect and enqueue
                                               * events from the
                                               * device*/
    dmxProcessInputProcPtr process_input;     /**< Process event (from
                                               * queue)  */
    dmxFunctionsProcPtr functions;
    dmxUpdateInfoProcPtr update_info;         /**< Update window layout
                                               * information */

    dmxMCtrlProcPtr mCtrl;                    /**< Pointer control */
    dmxKBCtrlProcPtr kCtrl;                   /**< Keyboard control */
    dmxKBBellProcPtr kBell;                   /**< Bell control */

    pointer private;                          /**< Device-dependent private  */
    int isCore;                               /**< Is a DMX core device  */
    int sendsCore;                            /**< Sends DMX core events */
    KeybdCtrl kctrl;                          /**< Keyboard control */
    PtrCtrl mctrl;                            /**< Pointer control */

    DeviceIntPtr pDevice;                     /**< X-level device  */
    int inputIdx;                             /**< High-level index */
    int lastX, lastY;                         /**< Last known position;
                                               * for XInput in
                                               * dmxevents.c */

    int head;                                 /**< XInput motion history
                                               * head */
    int tail;                                 /**< XInput motion history
                                               * tail */
    unsigned long *history;                   /**< XInput motion history */
    int *valuators;                           /**< Cache of previous values */

    /* for XInput ChangePointerDevice */
    int (*savedMotionProc) (DeviceIntPtr,
                            xTimecoord *,
                            unsigned long, unsigned long, ScreenPtr);
    int savedMotionEvents;                      /**< Saved motion events */
    int savedSendsCore;                         /**< Saved sends-core flag */

    DMXEventMap map[DMX_MAP_ENTRIES];              /**< XInput device id map */
    int mapOptimize;                               /**< XInput device id
                                                    * map
                                                    * optimization */

    long deviceId;                        /**< device id on remote side,
                                           * if any */
    const char *deviceName;               /**< devive name on remote
                                           * side, if any */
} DMXLocalInputInfoRec;

extern DMXLocalInputInfoPtr dmxLocalCorePointer, dmxLocalCoreKeyboard;

extern void dmxLocalInitInput(DMXInputInfo * dmxInput);
extern DMXLocalInputInfoPtr dmxInputCopyLocal(DMXInputInfo * dmxInput,
                                              DMXLocalInputInfoPtr s);

extern void dmxChangePointerControl(DeviceIntPtr pDevice, PtrCtrl * ctrl);
extern void dmxKeyboardKbdCtrlProc(DeviceIntPtr pDevice, KeybdCtrl * ctrl);
extern void dmxKeyboardBellProc(int percent, DeviceIntPtr pDevice,
                                pointer ctrl, int unknown);

extern int dmxInputExtensionErrorHandler(Display * dsp, _Xconst char *name,
                                         _Xconst char *reason);

extern int dmxInputDetach(DMXInputInfo * dmxInput);
extern void dmxInputDetachAll(DMXScreenInfo * dmxScreen);
extern int dmxInputDetachId(int id);
extern DMXInputInfo *dmxInputLocateId(int id);
extern int dmxInputAttachConsole(const char *name, int isCore, int *id);
extern int dmxInputAttachBackend(int physicalScreen, int isCore, int *id);

#endif
