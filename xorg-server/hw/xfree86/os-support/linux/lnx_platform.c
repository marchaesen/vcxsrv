#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#ifdef XSERVER_PLATFORM_BUS

#include <xf86drm.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Linux platform device support */
#include "xf86_OSproc.h"

#include "xf86.h"
#include "xf86platformBus.h"
#include "xf86Bus.h"

#include "hotplug.h"

static Bool
get_drm_info(struct OdevAttributes *attribs, char *path, int delayed_index)
{
    drmSetVersion sv;
    char *buf;
    int fd;
    int err = 0;

    fd = open(path, O_RDWR, O_CLOEXEC);
    if (fd == -1)
        return FALSE;

    sv.drm_di_major = 1;
    sv.drm_di_minor = 4;
    sv.drm_dd_major = -1;       /* Don't care */
    sv.drm_dd_minor = -1;       /* Don't care */

    err = drmSetInterfaceVersion(fd, &sv);
    if (err) {
        ErrorF("setversion 1.4 failed: %s\n", strerror(-err));
	goto out;
    }

    /* for a delayed probe we've already added the device */
    if (delayed_index == -1) {
            xf86_add_platform_device(attribs);
            delayed_index = xf86_num_platform_devices - 1;
    }

    buf = drmGetBusid(fd);
    xf86_add_platform_device_attrib(delayed_index,
                                    ODEV_ATTRIB_BUSID, buf);
    drmFreeBusid(buf);
out:
    close(fd);
    return (err == 0);
}

Bool
xf86PlatformDeviceCheckBusID(struct xf86_platform_device *device, const char *busid)
{
    struct OdevAttribute *attrib;
    const char *syspath = NULL;
    BusType bustype;
    const char *id;
    xorg_list_for_each_entry(attrib, &device->attribs->list, member) {
        if (attrib->attrib_id == ODEV_ATTRIB_SYSPATH) {
            syspath = attrib->attrib_name;
            break;
        }
    }

    if (!syspath)
        return FALSE;

    bustype = StringToBusType(busid, &id);
    if (bustype == BUS_PCI) {
        struct pci_device *pPci = device->pdev;
        if (xf86ComparePciBusString(busid,
                                    ((pPci->domain << 8)
                                     | pPci->bus),
                                    pPci->dev, pPci->func)) {
            return TRUE;
        }
    }
    else if (bustype == BUS_PLATFORM) {
        /* match on the minimum string */
        int len = strlen(id);

        if (strlen(syspath) < strlen(id))
            len = strlen(syspath);

        if (strncmp(id, syspath, len))
            return FALSE;
        return TRUE;
    }
    return FALSE;
}

void
xf86PlatformReprobeDevice(int index, struct OdevAttributes *attribs)
{
    Bool ret;
    char *dpath;
    dpath = xf86_get_platform_attrib(index, ODEV_ATTRIB_PATH);

    ret = get_drm_info(attribs, dpath, index);
    if (ret == FALSE) {
        xf86_remove_platform_device(index);
        return;
    }
    ret = xf86platformAddDevice(index);
    if (ret == -1)
        xf86_remove_platform_device(index);
}

void
xf86PlatformDeviceProbe(struct OdevAttributes *attribs)
{
    struct OdevAttribute *attrib;
    int i;
    char *path = NULL;
    Bool ret;

    xorg_list_for_each_entry(attrib, &attribs->list, member) {
        if (attrib->attrib_id == ODEV_ATTRIB_PATH) {
            path = attrib->attrib_name;
            break;
        }
    }
    if (!path)
        goto out_free;

    for (i = 0; i < xf86_num_platform_devices; i++) {
        char *dpath;
        dpath = xf86_get_platform_attrib(i, ODEV_ATTRIB_PATH);

        if (!strcmp(path, dpath))
            break;
    }

    if (i != xf86_num_platform_devices)
        goto out_free;

    LogMessage(X_INFO, "xfree86: Adding drm device (%s)\n", path);

    if (!xf86VTOwner()) {
            /* if we don't currently own the VT then don't probe the device,
               just mark it as unowned for later use */
            attribs->unowned = TRUE;
            xf86_add_platform_device(attribs);
            return;
    }

    ret = get_drm_info(attribs, path, -1);
    if (ret == FALSE)
        goto out_free;

    return;

out_free:
    config_odev_free_attribute_list(attribs);
}

void NewGPUDeviceRequest(struct OdevAttributes *attribs)
{
    int old_num = xf86_num_platform_devices;
    int ret;
    xf86PlatformDeviceProbe(attribs);

    if (old_num == xf86_num_platform_devices)
        return;

    if (xf86_get_platform_device_unowned(xf86_num_platform_devices - 1) == TRUE)
        return;

    ret = xf86platformAddDevice(xf86_num_platform_devices-1);
    if (ret == -1)
        xf86_remove_platform_device(xf86_num_platform_devices-1);

    ErrorF("xf86: found device %d\n", xf86_num_platform_devices);
    return;
}

void DeleteGPUDeviceRequest(struct OdevAttributes *attribs)
{
    struct OdevAttribute *attrib;
    int index;
    char *syspath = NULL;

    xorg_list_for_each_entry(attrib, &attribs->list, member) {
        if (attrib->attrib_id == ODEV_ATTRIB_SYSPATH) {
            syspath = attrib->attrib_name;
            break;
        }
    }

    for (index = 0; index < xf86_num_platform_devices; index++) {
        char *dspath;
        dspath = xf86_get_platform_attrib(index, ODEV_ATTRIB_SYSPATH);
        if (!strcmp(syspath, dspath))
            break;
    }

    if (index == xf86_num_platform_devices)
        goto out;

    ErrorF("xf86: remove device %d %s\n", index, syspath);

    if (xf86_get_platform_device_unowned(index) == TRUE)
            xf86_remove_platform_device(index);
    else
            xf86platformRemoveDevice(index);
out:
    config_odev_free_attribute_list(attribs);
}

#endif
