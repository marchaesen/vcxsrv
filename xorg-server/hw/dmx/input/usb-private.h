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
 * Private header file for USB support.  This file provides
 * Linux-specific include files and the definition of the private
 * structure.  \see usb-common.c \see usb-keyboard.c \see usb-mouse.c
 * \see usb-other.c */

#ifndef _USB_PRIVATE_H_
#define _USB_PRIVATE_H_

#include "dmxinputinit.h"
#include "inputstr.h"
#include <X11/Xos.h>
#include <errno.h>
#include <linux/input.h>
#include "usb-common.h"

                                /*  Support for force feedback was
                                 *  introduced in Linxu 2.4.10 */
#ifndef EV_MSC
#define EV_MSC      0x04
#endif
#ifndef EV_FF
#define EV_FF       0x15
#endif
#ifndef LED_SLEEP
#define LED_SLEEP   0x05
#endif
#ifndef LED_SUSPEND
#define LED_SUSPEND 0x06
#endif
#ifndef LED_MUTE
#define LED_MUTE    0x07
#endif
#ifndef LED_MISC
#define LED_MISC    0x08
#endif
#ifndef BTN_DEAD
#define BTN_DEAD    0x12f
#endif
#ifndef BTN_THUMBL
#define BTN_THUMBL  0x13d
#endif
#ifndef BTN_THUMBR
#define BTN_THUMBR  0x13e
#endif
#ifndef MSC_SERIAL
#define MSC_SERIAL  0x00
#endif
#ifndef MSC_MAX
#define MSC_MAX     0x07
#endif

                                /* Support for older kernels. */
#ifndef ABS_WHEEL
#define ABS_WHEEL   0x08
#endif
#ifndef ABS_GAS
#define ABS_GAS     0x09
#endif
#ifndef ABS_BRAKE
#define ABS_BRAKE   0x0a
#endif

#define NUM_STATE_ENTRIES (256/32)

/* Private area for USB devices. */
typedef struct _myPrivate {
    DeviceIntPtr pDevice;                   /**< Device (mouse or other) */
    int fd;                                 /**< File descriptor */
    unsigned char mask[EV_MAX / 8 + 1];     /**< Mask */
    int numRel, numAbs, numLeds;            /**< Counts */
    int relmap[DMX_MAX_AXES];               /**< Relative axis map */
    int absmap[DMX_MAX_AXES];               /**< Absolute axis map */

    CARD32 kbdState[NUM_STATE_ENTRIES];         /**< Keyboard state */
    DeviceIntPtr pKeyboard;                     /** Keyboard device */

    int pitch;                  /**< Bell pitch  */
    unsigned long duration;     /**< Bell duration */

    /* FIXME: dmxInput is never initialized */
    DMXInputInfo *dmxInput;     /**< For pretty-printing */
} myPrivate;
#endif
