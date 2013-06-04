/*
 * Copyright © 2006-2007 Daniel Stone
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Daniel Stone <daniel@fooishbar.org>
 */

#ifndef HOTPLUG_H
#define HOTPLUG_H

#include "list.h"

extern _X_EXPORT void config_pre_init(void);
extern _X_EXPORT void config_init(void);
extern _X_EXPORT void config_fini(void);

struct OdevAttribute {
    struct xorg_list member;
    int attrib_id;
    char *attrib_name;
};

struct OdevAttributes {
    struct xorg_list list;
    Bool unowned;
};

struct OdevAttributes *
config_odev_allocate_attribute_list(void);

void
config_odev_free_attribute_list(struct OdevAttributes *attribs);

Bool
config_odev_add_attribute(struct OdevAttributes *attribs, int attrib,
                          const char *attrib_name);

void
config_odev_free_attributes(struct OdevAttributes *attribs);

/* path to kernel device node - Linux e.g. /dev/dri/card0 */
#define ODEV_ATTRIB_PATH 1
/* system device path - Linux e.g. /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/drm/card1 */
#define ODEV_ATTRIB_SYSPATH 2
/* DRI-style bus id */
#define ODEV_ATTRIB_BUSID 3

typedef void (*config_odev_probe_proc_ptr)(struct OdevAttributes *attribs);
void config_odev_probe(config_odev_probe_proc_ptr probe_callback);

#ifdef CONFIG_UDEV_KMS
void NewGPUDeviceRequest(struct OdevAttributes *attribs);
void DeleteGPUDeviceRequest(struct OdevAttributes *attribs);
#endif

#define ServerIsNotSeat0() (SeatId && strcmp(SeatId, "seat0"))
#endif                          /* HOTPLUG_H */
