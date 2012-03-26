/*
 * Copyright 2001,2002 Red Hat Inc., Durham, North Carolina.
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
 *   David H. Dawes <dawes@xfree86.org>
 *   Kevin E. Martin <kem@redhat.com>
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * This file provides access to:
 * - global variables available to all hw/dmx routines, and
 * - enumerations and typedefs needed by input routines in hw/dmx (and
 *   hw/dmx/input).
 *
 * The goal is that no files in hw/dmx should include header files from
 * hw/dmx/input -- the interface defined here should be the only
 * interface exported to the hw/dmx layer.  \see input/dmxinputinit.c.
 */

#ifndef DMXINPUT_H
#define DMXINPUT_H

/** Maximum number of file descriptors for SIGIO handling */
#define DMX_MAX_SIGIO_FDS 4

struct _DMXInputInfo;

/** Reason why window layout was updated. */
typedef enum {
    DMX_UPDATE_REALIZE,         /**< Window realized        */
    DMX_UPDATE_UNREALIZE,       /**< Window unrealized      */
    DMX_UPDATE_RESTACK,         /**< Stacking order changed */
    DMX_UPDATE_COPY,            /**< Window copied          */
    DMX_UPDATE_RESIZE,          /**< Window resized         */
    DMX_UPDATE_REPARENT         /**< Window reparented      */
} DMXUpdateType;

typedef void (*ProcessInputEventsProc) (struct _DMXInputInfo *);
typedef void (*UpdateWindowInfoProc) (struct _DMXInputInfo *,
                                      DMXUpdateType, WindowPtr);

/** An opaque structure that is only exposed in the dmx/input layer. */
typedef struct _DMXLocalInputInfo *DMXLocalInputInfoPtr;

/** State of the SIGIO engine */
typedef enum {
    DMX_NOSIGIO = 0,            /**< Device does not use SIGIO at all. */
    DMX_USESIGIO,               /**< Device can use SIGIO, but is not
                                 * (e.g., because the VT is switch
                                 * away). */
    DMX_ACTIVESIGIO             /**< Device is currently using SIGIO. */
} dmxSigioState;

/** DMXInputInfo is typedef'd in \a dmx.h so that all routines can have
 * access to the global pointers.  However, the elements are only
 * available to input-related routines. */
struct _DMXInputInfo {
    const char *name;              /**< Name of input display or device
                                    * (from command line or config
                                    * file)  */
    Bool freename;                    /**< If true, free name on destroy */
    Bool detached;                    /**< If true, input screen is detached */
    int inputIdx;                     /**< Index into #dmxInputs global */
    int scrnIdx;                      /**< Index into #dmxScreens global */
    Bool core;                     /**< If True, initialize these
                                    * devices as devices that send core
                                    * events */
    Bool console;                    /**< True if console and backend
                                      * input share the same backend
                                      * display  */

    Bool windows;                    /**< True if window outlines are
                                      * draw in console */

    ProcessInputEventsProc processInputEvents;
    UpdateWindowInfoProc updateWindowInfo;

    /* Local input information */
    dmxSigioState sigioState;              /**< Current stat */
    int sigioFdCount;                      /**< Number of fds in use */
    int sigioFd[DMX_MAX_SIGIO_FDS];                        /**< List of fds */
    Bool sigioAdded[DMX_MAX_SIGIO_FDS];                    /**< Active fds */

    /** True if a VT switch is pending, but has not yet happened. */
    int vt_switch_pending;

    /** True if a VT switch has happened. */
    int vt_switched;

    /** Number of devices handled in this _DMXInputInfo structure. */
    int numDevs;

    /** List of actual input devices.  Each _DMXInputInfo structure can
     * refer to more than one device.  For example, the keyboard and the
     * pointer of a backend display; or all of the XInput extension
     * devices on a backend display. */
    DMXLocalInputInfoPtr *devs;

    char *keycodes;                    /**< XKB keycodes from command line */
    char *symbols;                     /**< XKB symbols from command line */
    char *geometry;                    /**< XKB geometry from command line */
};

extern int dmxNumInputs;                  /**< Number of #dmxInputs */
extern DMXInputInfo *dmxInputs;           /**< List of inputs */

extern void dmxInputInit(DMXInputInfo * dmxInput);
extern void dmxInputReInit(DMXInputInfo * dmxInput);
extern void dmxInputLateReInit(DMXInputInfo * dmxInput);
extern void dmxInputFree(DMXInputInfo * dmxInput);
extern void dmxInputLogDevices(void);
extern void dmxUpdateWindowInfo(DMXUpdateType type, WindowPtr pWindow);

/* These functions are defined in input/dmxeq.c */
extern void dmxeqSwitchScreen(DeviceIntPtr pDev, ScreenPtr pScreen,
                              Bool fromDIX);

/* This type is used in input/dmxevents.c.  Also, these functions are
 * defined in input/dmxevents.c */
typedef enum {
    DMX_NO_BLOCK = 0,
    DMX_BLOCK = 1
} DMXBlockType;

extern void dmxGetGlobalPosition(int *x, int *y);
extern DMXScreenInfo *dmxFindFirstScreen(int x, int y);
extern void dmxCoreMotion(DevicePtr pDev, int x, int y, int delta,
                          DMXBlockType block);

/* Support for dynamic addition of inputs.  This functions is defined in
 * config/dmxconfig.c */
extern DMXInputInfo *dmxConfigAddInput(const char *name, int core);
#endif                          /* DMXINPUT_H */
