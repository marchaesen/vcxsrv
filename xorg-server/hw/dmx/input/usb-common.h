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
 * Interface to common USB support.  \see usb-common.c \see usb-mouse.c
 * \see usb-keyboard.c \see usb-other.c */

#ifndef _USB_COMMON_H_
#define _USB_COMMON_H_
typedef enum {
    usbMouse,
    usbKeyboard,
    usbOther
} usbType;

extern void *usbCreatePrivate(DeviceIntPtr pDevice);
extern void usbDestroyPrivate(void *priv);
extern void usbRead(DevicePtr pDev,
                    dmxMotionProcPtr motion,
                    dmxEnqueueProcPtr enqueue,
                    int minButton, DMXBlockType block);
extern void usbInit(DevicePtr pDev, usbType type);
extern void usbOff(DevicePtr pDev);
#endif
