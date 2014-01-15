/*
 * Copyright 2001 Red Hat Inc., Durham, North Carolina.
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
 * Interface to Linux PS/2 mouse driver.  \see lnx-ps2.c */

#ifndef _LNX_PS2_H_
#define _LNX_PS2_H_

extern void *ps2LinuxCreatePrivate(DeviceIntPtr pMouse);
extern void ps2LinuxDestroyPrivate(void *priv);
extern void ps2LinuxRead(DevicePtr pDev,
                         dmxMotionProcPtr motion,
                         dmxEnqueueProcPtr enqueue,
                         dmxCheckSpecialProcPtr checkspecial,
                         DMXBlockType block);
extern void ps2LinuxInit(DevicePtr pDev);
extern void ps2LinuxGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info);
extern int ps2LinuxOn(DevicePtr pDev);
extern void ps2LinuxOff(DevicePtr pDev);
extern void ps2LinuxCtrl(DevicePtr pDev, PtrCtrl * ctrl);
extern void ps2LinuxVTPreSwitch(void *p);
extern void ps2LinuxVTPostSwitch(void *p);

#endif
