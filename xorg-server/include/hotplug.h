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

enum { ODEV_ATTRIB_STRING, ODEV_ATTRIB_INT };

struct OdevAttribute {
    struct xorg_list member;
    int attrib_id;
    union {
        char *attrib_name;
        int attrib_value;
    };
    int attrib_type;
};

struct OdevAttributes {
    struct xorg_list list;
};

/* Note starting with xserver 1.16 this function never fails */
struct OdevAttributes *
config_odev_allocate_attribute_list(void);

void
config_odev_free_attribute_list(struct OdevAttributes *attribs);

/* Note starting with xserver 1.16 this function never fails */
Bool
config_odev_add_attribute(struct OdevAttributes *attribs, int attrib,
                          const char *attrib_name);

char *
config_odev_get_attribute(struct OdevAttributes *attribs, int attrib_id);

/* Note starting with xserver 1.16 this function never fails */
Bool
config_odev_add_int_attribute(struct OdevAttributes *attribs, int attrib,
                              int attrib_value);

int
config_odev_get_int_attribute(struct OdevAttributes *attribs, int attrib,
                              int def);

void
config_odev_free_attributes(struct OdevAttributes *attribs);

/* path to kernel device node - Linux e.g. /dev/dri/card0 */
#define ODEV_ATTRIB_PATH 1
/* system device path - Linux e.g. /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/drm/card1 */
#define ODEV_ATTRIB_SYSPATH 2
/* DRI-style bus id */
#define ODEV_ATTRIB_BUSID 3
/* Server managed FD */
#define ODEV_ATTRIB_FD 4
/* Major number of the device node pointed to by ODEV_ATTRIB_PATH */
#define ODEV_ATTRIB_MAJOR 5
/* Minor number of the device node pointed to by ODEV_ATTRIB_PATH */
#define ODEV_ATTRIB_MINOR 6

typedef void (*config_odev_probe_proc_ptr)(struct OdevAttributes *attribs);
void config_odev_probe(config_odev_probe_proc_ptr probe_callback);

#ifdef CONFIG_UDEV_KMS
void NewGPUDeviceRequest(struct OdevAttributes *attribs);
void DeleteGPUDeviceRequest(struct OdevAttributes *attribs);
#endif

#define ServerIsNotSeat0() (SeatId && strcmp(SeatId, "seat0"))

struct xf86_platform_device *
xf86_find_platform_device_by_devnum(int major, int minor);

#endif                          /* HOTPLUG_H */
