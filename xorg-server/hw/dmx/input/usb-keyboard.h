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
 * Interface to USB keyboard driver. \see usb-keyboard.c \see usb-common.c */

#ifndef _USB_KEYBOARD_H_
#define _USB_KEYBOARD_H_
extern void kbdUSBInit(DevicePtr pDev);
extern void kbdUSBGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info);
extern int kbdUSBOn(DevicePtr pDev);
extern void kbdUSBRead(DevicePtr pDev,
                       dmxMotionProcPtr motion,
                       dmxEnqueueProcPtr enqueue,
                       dmxCheckSpecialProcPtr checkspecial, DMXBlockType block);
extern void kbdUSBCtrl(DevicePtr pDev, KeybdCtrl * ctrl);
#endif
