/*
 * Copyright © 2012 Red Hat.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Dave Airlie <airlied@redhat.com>
 */
#ifndef XF86_PLATFORM_BUS_H
#define XF86_PLATFORM_BUS_H

#include "hotplug.h"

struct xf86_platform_device {
    struct OdevAttributes *attribs;
    /* for PCI devices */
    struct pci_device *pdev;
};

#ifdef XSERVER_PLATFORM_BUS
int xf86platformProbe(void);
int xf86platformProbeDev(DriverPtr drvp);

extern int xf86_num_platform_devices;

extern char *
xf86_get_platform_attrib(int index, int attrib_id);
extern int
xf86_add_platform_device(struct OdevAttributes *attribs);
extern int
xf86_remove_platform_device(int dev_index);
extern Bool
xf86_add_platform_device_attrib(int index, int attrib_id, char *attrib_str);
extern Bool
xf86_get_platform_device_unowned(int index);

extern int
xf86platformAddDevice(int index);
extern void
xf86platformRemoveDevice(int index);

extern _X_EXPORT char *
xf86_get_platform_device_attrib(struct xf86_platform_device *device, int attrib_id);
extern _X_EXPORT Bool
xf86PlatformDeviceCheckBusID(struct xf86_platform_device *device, const char *busid);

extern _X_EXPORT int
xf86PlatformMatchDriver(char *matches[], int nmatches);

extern void xf86platformVTProbe(void);
#endif

#endif
