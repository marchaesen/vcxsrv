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

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <unistd.h>
#include "os.h"
#include "inputstr.h"
#include "hotplug.h"
#include "config-backends.h"
#include "systemd-logind.h"

void
config_pre_init(void)
{
#ifdef CONFIG_UDEV
    if (!config_udev_pre_init())
        ErrorF("[config] failed to pre-init udev\n");
#endif
}

void
config_init(void)
{
#ifdef CONFIG_UDEV
    if (!config_udev_init())
        ErrorF("[config] failed to initialise udev\n");
#elif defined(CONFIG_HAL)
    if (!config_hal_init())
        ErrorF("[config] failed to initialise HAL\n");
#elif defined(CONFIG_WSCONS)
    if (!config_wscons_init())
        ErrorF("[config] failed to initialise wscons\n");
#endif
}

void
config_fini(void)
{
#if defined(CONFIG_UDEV)
    config_udev_fini();
#elif defined(CONFIG_HAL)
    config_hal_fini();
#elif defined(CONFIG_WSCONS)
    config_wscons_fini();
#endif
}

void
config_odev_probe(config_odev_probe_proc_ptr probe_callback)
{
#if defined(CONFIG_UDEV_KMS)
    config_udev_odev_probe(probe_callback);
#endif
}

static void
remove_device(const char *backend, DeviceIntPtr dev)
{
    /* this only gets called for devices that have already been added */
    LogMessage(X_INFO, "config/%s: removing device %s\n", backend, dev->name);

    /* Call PIE here so we don't try to dereference a device that's
     * already been removed. */
    OsBlockSignals();
    ProcessInputEvents();
    DeleteInputDeviceRequest(dev);
    OsReleaseSignals();
}

void
remove_devices(const char *backend, const char *config_info)
{
    DeviceIntPtr dev, next;

    for (dev = inputInfo.devices; dev; dev = next) {
        next = dev->next;
        if (dev->config_info && strcmp(dev->config_info, config_info) == 0)
            remove_device(backend, dev);
    }
    for (dev = inputInfo.off_devices; dev; dev = next) {
        next = dev->next;
        if (dev->config_info && strcmp(dev->config_info, config_info) == 0)
            remove_device(backend, dev);
    }
}

BOOL
device_is_duplicate(const char *config_info)
{
    DeviceIntPtr dev;

    for (dev = inputInfo.devices; dev; dev = dev->next) {
        if (dev->config_info && (strcmp(dev->config_info, config_info) == 0))
            return TRUE;
    }

    for (dev = inputInfo.off_devices; dev; dev = dev->next) {
        if (dev->config_info && (strcmp(dev->config_info, config_info) == 0))
            return TRUE;
    }

    return FALSE;
}

struct OdevAttributes *
config_odev_allocate_attribute_list(void)
{
    struct OdevAttributes *attriblist;

    attriblist = XNFalloc(sizeof(struct OdevAttributes));
    xorg_list_init(&attriblist->list);
    return attriblist;
}

void
config_odev_free_attribute_list(struct OdevAttributes *attribs)
{
    config_odev_free_attributes(attribs);
    free(attribs);
}

static struct OdevAttribute *
config_odev_find_attribute(struct OdevAttributes *attribs, int attrib_id)
{
    struct OdevAttribute *oa;

    xorg_list_for_each_entry(oa, &attribs->list, member) {
        if (oa->attrib_id == attrib_id)
          return oa;
    }
    return NULL;
}

static struct OdevAttribute *
config_odev_find_or_add_attribute(struct OdevAttributes *attribs, int attrib)
{
    struct OdevAttribute *oa;

    oa = config_odev_find_attribute(attribs, attrib);
    if (oa)
        return oa;

    oa = XNFcalloc(sizeof(struct OdevAttribute));
    oa->attrib_id = attrib;
    xorg_list_append(&oa->member, &attribs->list);

    return oa;
}

Bool
config_odev_add_attribute(struct OdevAttributes *attribs, int attrib,
                          const char *attrib_name)
{
    struct OdevAttribute *oa;

    oa = config_odev_find_or_add_attribute(attribs, attrib);
    free(oa->attrib_name);
    oa->attrib_name = XNFstrdup(attrib_name);
    oa->attrib_type = ODEV_ATTRIB_STRING;
    return TRUE;
}

Bool
config_odev_add_int_attribute(struct OdevAttributes *attribs, int attrib,
                              int attrib_value)
{
    struct OdevAttribute *oa;

    oa = config_odev_find_or_add_attribute(attribs, attrib);
    oa->attrib_value = attrib_value;
    oa->attrib_type = ODEV_ATTRIB_INT;
    return TRUE;
}

char *
config_odev_get_attribute(struct OdevAttributes *attribs, int attrib_id)
{
    struct OdevAttribute *oa;

    oa = config_odev_find_attribute(attribs, attrib_id);
    if (!oa)
        return NULL;

    if (oa->attrib_type != ODEV_ATTRIB_STRING) {
        LogMessage(X_ERROR, "Error %s called for non string attrib %d\n",
                   __func__, attrib_id);
        return NULL;
    }
    return oa->attrib_name;
}

int
config_odev_get_int_attribute(struct OdevAttributes *attribs, int attrib_id, int def)
{
    struct OdevAttribute *oa;

    oa = config_odev_find_attribute(attribs, attrib_id);
    if (!oa)
        return def;

    if (oa->attrib_type != ODEV_ATTRIB_INT) {
        LogMessage(X_ERROR, "Error %s called for non integer attrib %d\n",
                   __func__, attrib_id);
        return def;
    }

    return oa->attrib_value;
}

void
config_odev_free_attributes(struct OdevAttributes *attribs)
{
    struct OdevAttribute *iter, *safe;
    int major = 0, minor = 0, fd = -1;

    xorg_list_for_each_entry_safe(iter, safe, &attribs->list, member) {
        switch (iter->attrib_id) {
        case ODEV_ATTRIB_MAJOR: major = iter->attrib_value; break;
        case ODEV_ATTRIB_MINOR: minor = iter->attrib_value; break;
        case ODEV_ATTRIB_FD: fd = iter->attrib_value; break;
        }
        xorg_list_del(&iter->member);
        if (iter->attrib_type == ODEV_ATTRIB_STRING)
            free(iter->attrib_name);
        free(iter);
    }

    if (fd != -1)
        systemd_logind_release_fd(major, minor, fd);
}
