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

/*
 * This file contains the interfaces to the bus-specific code
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#ifdef XSERVER_PLATFORM_BUS
#include <errno.h>

#include <pciaccess.h>
#include <fcntl.h>
#include <unistd.h>
#include "os.h"
#include "hotplug.h"
#include "systemd-logind.h"

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Priv.h"
#include "xf86str.h"
#include "xf86Bus.h"
#include "Pci.h"
#include "xf86platformBus.h"
#include "xf86Config.h"

#include "randrstr.h"
int platformSlotClaimed;

int xf86_num_platform_devices;

struct xf86_platform_device *xf86_platform_devices;

int
xf86_add_platform_device(struct OdevAttributes *attribs, Bool unowned)
{
    xf86_platform_devices = xnfreallocarray(xf86_platform_devices,
                                            xf86_num_platform_devices + 1,
                                            sizeof(struct xf86_platform_device));

    xf86_platform_devices[xf86_num_platform_devices].attribs = attribs;
    xf86_platform_devices[xf86_num_platform_devices].pdev = NULL;
    xf86_platform_devices[xf86_num_platform_devices].flags =
        unowned ? XF86_PDEV_UNOWNED : 0;

    xf86_num_platform_devices++;
    return 0;
}

int
xf86_remove_platform_device(int dev_index)
{
    int j;

    config_odev_free_attributes(xf86_platform_devices[dev_index].attribs);

    for (j = dev_index; j < xf86_num_platform_devices - 1; j++)
        memcpy(&xf86_platform_devices[j], &xf86_platform_devices[j + 1], sizeof(struct xf86_platform_device));
    xf86_num_platform_devices--;
    return 0;
}

Bool
xf86_get_platform_device_unowned(int index)
{
    return (xf86_platform_devices[index].flags & XF86_PDEV_UNOWNED) ?
        TRUE : FALSE;
}

struct xf86_platform_device *
xf86_find_platform_device_by_devnum(int major, int minor)
{
    int i, attr_major, attr_minor;

    for (i = 0; i < xf86_num_platform_devices; i++) {
        attr_major = xf86_platform_odev_attributes(i)->major;
        attr_minor = xf86_platform_odev_attributes(i)->minor;
        if (attr_major == major && attr_minor == minor)
            return &xf86_platform_devices[i];
    }
    return NULL;
}

/*
 * xf86IsPrimaryPlatform() -- return TRUE if primary device
 * is a platform device and it matches this one.
 */

static Bool
xf86IsPrimaryPlatform(struct xf86_platform_device *plat)
{
    return ((primaryBus.type == BUS_PLATFORM) && (plat == primaryBus.id.plat));
}

static void
platform_find_pci_info(struct xf86_platform_device *pd, char *busid)
{
    struct pci_slot_match devmatch;
    struct pci_device *info;
    struct pci_device_iterator *iter;
    int ret;

    ret = sscanf(busid, "pci:%04x:%02x:%02x.%u",
                 &devmatch.domain, &devmatch.bus, &devmatch.dev,
                 &devmatch.func);
    if (ret != 4)
        return;

    iter = pci_slot_match_iterator_create(&devmatch);
    info = pci_device_next(iter);
    if (info) {
        pd->pdev = info;
        pci_device_probe(info);
        if (pci_device_is_boot_vga(info)) {
            primaryBus.type = BUS_PLATFORM;
            primaryBus.id.plat = pd;
        }
    }
    pci_iterator_destroy(iter);

}

static Bool
xf86_check_platform_slot(const struct xf86_platform_device *pd)
{
    int i;

    for (i = 0; i < xf86NumEntities; i++) {
        const EntityPtr u = xf86Entities[i];

        if (pd->pdev && u->bus.type == BUS_PCI &&
            MATCH_PCI_DEVICES(pd->pdev, u->bus.id.pci)) {
            return FALSE;
        }
        if ((u->bus.type == BUS_PLATFORM) && (pd == u->bus.id.plat)) {
            return FALSE;
        }
    }
    return TRUE;
}

static Bool
MatchToken(const char *value, struct xorg_list *patterns,
           int (*compare)(const char *, const char *))
{
    const xf86MatchGroup *group;

    /* If there are no patterns, accept the match */
    if (xorg_list_is_empty(patterns))
        return TRUE;

    /* If there are patterns but no attribute, reject the match */
    if (!value)
        return FALSE;

    /*
     * Otherwise, iterate the list of patterns ensuring each entry has a
     * match. Each list entry is a separate Match line of the same type.
     */
    xorg_list_for_each_entry(group, patterns, entry) {
        Bool match = FALSE;
        char *const *cur;

        for (cur = group->values; *cur; cur++) {
            if ((*compare)(value, *cur) == 0) {
                match = TRUE;
                break;
            }
        }

        if (!match)
            return FALSE;
    }

    /* All the entries in the list matched the attribute */
    return TRUE;
}

static Bool
OutputClassMatches(const XF86ConfOutputClassPtr oclass, int index)
{
    char *driver = xf86_platform_odev_attributes(index)->driver;

    if (!MatchToken(driver, &oclass->match_driver, strcmp))
        return FALSE;

    return TRUE;
}

static int
xf86OutputClassDriverList(int index, char *matches[], int nmatches)
{
    XF86ConfOutputClassPtr cl;
    int i = 0;

    if (nmatches == 0)
        return 0;

    for (cl = xf86configptr->conf_outputclass_lst; cl; cl = cl->list.next) {
        if (OutputClassMatches(cl, index)) {
            char *path = xf86_platform_odev_attributes(index)->path;

            xf86Msg(X_INFO, "Applying OutputClass \"%s\" to %s\n",
                    cl->identifier, path);
            xf86Msg(X_NONE, "\tloading driver: %s\n", cl->driver);

            matches[i++] = xstrdup(cl->driver);
        }

        if (i >= nmatches)
            break;
    }

    return i;
}

/**
 *  @return The numbers of found devices that match with the current system
 *  drivers.
 */
int
xf86PlatformMatchDriver(char *matches[], int nmatches)
{
    int i, j = 0;
    struct pci_device *info = NULL;
    int pass = 0;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < xf86_num_platform_devices; i++) {

            if (xf86IsPrimaryPlatform(&xf86_platform_devices[i]) && (pass == 1))
                continue;
            else if (!xf86IsPrimaryPlatform(&xf86_platform_devices[i]) && (pass == 0))
                continue;

            j += xf86OutputClassDriverList(i, &matches[j], nmatches - j);

            info = xf86_platform_devices[i].pdev;
#ifdef __linux__
            if (info)
                j += xf86MatchDriverFromFiles(info->vendor_id, info->device_id,
                                              &matches[j], nmatches - j);
#endif

            if ((info != NULL) && (j < nmatches)) {
                j += xf86VideoPtrToDriverList(info, &(matches[j]), nmatches - j);
            }
        }
    }
    return j;
}

int
xf86platformProbe(void)
{
    int i;
    Bool pci = TRUE;

    config_odev_probe(xf86PlatformDeviceProbe);

    if (!xf86scanpci()) {
        pci = FALSE;
    }

    for (i = 0; i < xf86_num_platform_devices; i++) {
        char *busid = xf86_platform_odev_attributes(i)->busid;

        if (pci && (strncmp(busid, "pci:", 4) == 0)) {
            platform_find_pci_info(&xf86_platform_devices[i], busid);
        }
    }
    return 0;
}

static int
xf86ClaimPlatformSlot(struct xf86_platform_device * d, DriverPtr drvp,
                  int chipset, GDevPtr dev, Bool active)
{
    EntityPtr p = NULL;
    int num;

    if (xf86_check_platform_slot(d)) {
        num = xf86AllocateEntity();
        p = xf86Entities[num];
        p->driver = drvp;
        p->chipset = chipset;
        p->bus.type = BUS_PLATFORM;
        p->bus.id.plat = d;
        p->active = active;
        p->inUse = FALSE;
        if (dev)
            xf86AddDevToEntity(num, dev);

        platformSlotClaimed++;
        return num;
    }
    else
        return -1;
}

static int
xf86UnclaimPlatformSlot(struct xf86_platform_device *d, GDevPtr dev)
{
    int i;

    for (i = 0; i < xf86NumEntities; i++) {
        const EntityPtr p = xf86Entities[i];

        if ((p->bus.type == BUS_PLATFORM) && (p->bus.id.plat == d)) {
            if (dev)
                xf86RemoveDevFromEntity(i, dev);
            platformSlotClaimed--;
            p->bus.type = BUS_NONE;
            return 0;
        }
    }
    return 0;
}


#define END_OF_MATCHES(m)                                               \
    (((m).vendor_id == 0) && ((m).device_id == 0) && ((m).subvendor_id == 0))

static Bool doPlatformProbe(struct xf86_platform_device *dev, DriverPtr drvp,
                            GDevPtr gdev, int flags, intptr_t match_data)
{
    Bool foundScreen = FALSE;
    int entity;

    if (gdev && gdev->screen == 0 && !xf86_check_platform_slot(dev))
        return FALSE;

    entity = xf86ClaimPlatformSlot(dev, drvp, 0,
                                   gdev, gdev ? gdev->active : 0);

    if ((entity == -1) && gdev && (gdev->screen > 0)) {
        unsigned nent;

        for (nent = 0; nent < xf86NumEntities; nent++) {
            EntityPtr pEnt = xf86Entities[nent];

            if (pEnt->bus.type != BUS_PLATFORM)
                continue;
            if (pEnt->bus.id.plat == dev) {
                entity = nent;
                xf86AddDevToEntity(nent, gdev);
                break;
            }
        }
    }
    if (entity != -1) {
        if ((dev->flags & XF86_PDEV_SERVER_FD) && (!drvp->driverFunc ||
                !drvp->driverFunc(NULL, SUPPORTS_SERVER_FDS, NULL))) {
            systemd_logind_release_fd(dev->attribs->major, dev->attribs->minor, dev->attribs->fd);
            dev->attribs->fd = -1;
            dev->flags &= ~XF86_PDEV_SERVER_FD;
        }

        if (drvp->platformProbe(drvp, entity, flags, dev, match_data))
            foundScreen = TRUE;
        else
            xf86UnclaimPlatformSlot(dev, gdev);
    }
    return foundScreen;
}

static Bool
probeSingleDevice(struct xf86_platform_device *dev, DriverPtr drvp, GDevPtr gdev, int flags)
{
    int k;
    Bool foundScreen = FALSE;
    struct pci_device *pPci;
    const struct pci_id_match *const devices = drvp->supported_devices;

    if (dev->pdev && devices) {
        int device_id = dev->pdev->device_id;
        pPci = dev->pdev;
        for (k = 0; !END_OF_MATCHES(devices[k]); k++) {
            if (PCI_ID_COMPARE(devices[k].vendor_id, pPci->vendor_id)
                && PCI_ID_COMPARE(devices[k].device_id, device_id)
                && ((devices[k].device_class_mask & pPci->device_class)
                    ==  devices[k].device_class)) {
                foundScreen = doPlatformProbe(dev, drvp, gdev, flags, devices[k].match_data);
                if (foundScreen)
                    break;
            }
        }
    }
    else if (dev->pdev && !devices)
        return FALSE;
    else
        foundScreen = doPlatformProbe(dev, drvp, gdev, flags, 0);
    return foundScreen;
}

int
xf86platformProbeDev(DriverPtr drvp)
{
    Bool foundScreen = FALSE;
    GDevPtr *devList;
    const unsigned numDevs = xf86MatchDevice(drvp->driverName, &devList);
    int i, j;

    /* find the main device or any device specificed in xorg.conf */
    for (i = 0; i < numDevs; i++) {
        /* skip inactive devices */
        if (!devList[i]->active)
            continue;

        for (j = 0; j < xf86_num_platform_devices; j++) {
            if (devList[i]->busID && *devList[i]->busID) {
                if (xf86PlatformDeviceCheckBusID(&xf86_platform_devices[j], devList[i]->busID))
                    break;
            }
            else {
                /* for non-seat0 servers assume first device is the master */
                if (ServerIsNotSeat0())
                    break;

                if (xf86IsPrimaryPlatform(&xf86_platform_devices[j]))
                    break;
            }
        }

        if (j == xf86_num_platform_devices)
             continue;

        foundScreen = probeSingleDevice(&xf86_platform_devices[j], drvp, devList[i], 0);
        if (!foundScreen)
            continue;
    }

    /* if autoaddgpu devices is enabled then go find any unclaimed platform
     * devices and add them as GPU screens */
    if (xf86Info.autoAddGPU) {
        for (j = 0; j < xf86_num_platform_devices; j++) {
            if (probeSingleDevice(&xf86_platform_devices[j], drvp,
                                  devList ?  devList[0] : NULL,
                                  PLATFORM_PROBE_GPU_SCREEN))
                foundScreen = TRUE;
        }
    }

    return foundScreen;
}

int
xf86platformAddDevice(int index)
{
    int i, old_screens, scr_index;
    DriverPtr drvp = NULL;
    screenLayoutPtr layout;
    static const char *hotplug_driver_name = "modesetting";

    if (!xf86Info.autoAddGPU)
        return -1;

    /* force load the driver for now */
    xf86LoadOneModule(hotplug_driver_name, NULL);

    for (i = 0; i < xf86NumDrivers; i++) {
        if (!xf86DriverList[i])
            continue;

        if (!strcmp(xf86DriverList[i]->driverName, hotplug_driver_name)) {
            drvp = xf86DriverList[i];
            break;
        }
    }
    if (i == xf86NumDrivers)
        return -1;

    old_screens = xf86NumGPUScreens;
    doPlatformProbe(&xf86_platform_devices[index], drvp, NULL,
                    PLATFORM_PROBE_GPU_SCREEN, 0);
    if (old_screens == xf86NumGPUScreens)
        return -1;
    i = old_screens;

    for (layout = xf86ConfigLayout.screens; layout->screen != NULL;
         layout++) {
        xf86GPUScreens[i]->confScreen = layout->screen;
        break;
    }

    if (xf86GPUScreens[i]->PreInit &&
        xf86GPUScreens[i]->PreInit(xf86GPUScreens[i], 0))
        xf86GPUScreens[i]->configured = TRUE;

    if (!xf86GPUScreens[i]->configured) {
        ErrorF("hotplugged device %d didn't configure\n", i);
        xf86DeleteScreen(xf86GPUScreens[i]);
        return -1;
    }

   scr_index = AddGPUScreen(xf86GPUScreens[i]->ScreenInit, 0, NULL);
   if (scr_index == -1) {
       xf86DeleteScreen(xf86GPUScreens[i]);
       xf86UnclaimPlatformSlot(&xf86_platform_devices[index], NULL);
       xf86NumGPUScreens = old_screens;
       return -1;
   }
   dixSetPrivate(&xf86GPUScreens[i]->pScreen->devPrivates,
                 xf86ScreenKey, xf86GPUScreens[i]);

   CreateScratchPixmapsForScreen(xf86GPUScreens[i]->pScreen);

   if (xf86GPUScreens[i]->pScreen->CreateScreenResources &&
       !(*xf86GPUScreens[i]->pScreen->CreateScreenResources) (xf86GPUScreens[i]->pScreen)) {
       RemoveGPUScreen(xf86GPUScreens[i]->pScreen);
       xf86DeleteScreen(xf86GPUScreens[i]);
       xf86UnclaimPlatformSlot(&xf86_platform_devices[index], NULL);
       xf86NumGPUScreens = old_screens;
       return -1;
   }
   /* attach unbound to 0 protocol screen */
   AttachUnboundGPU(xf86Screens[0]->pScreen, xf86GPUScreens[i]->pScreen);

   RRResourcesChanged(xf86Screens[0]->pScreen);
   RRTellChanged(xf86Screens[0]->pScreen);

   return 0;
}

void
xf86platformRemoveDevice(int index)
{
    EntityPtr entity;
    int ent_num, i, j;
    Bool found;

    for (ent_num = 0; ent_num < xf86NumEntities; ent_num++) {
        entity = xf86Entities[ent_num];
        if (entity->bus.type == BUS_PLATFORM &&
            entity->bus.id.plat == &xf86_platform_devices[index])
            break;
    }
    if (ent_num == xf86NumEntities)
        goto out;

    found = FALSE;
    for (i = 0; i < xf86NumGPUScreens; i++) {
        for (j = 0; j < xf86GPUScreens[i]->numEntities; j++)
            if (xf86GPUScreens[i]->entityList[j] == ent_num) {
                found = TRUE;
                break;
            }
        if (found)
            break;
    }
    if (!found) {
        ErrorF("failed to find screen to remove\n");
        goto out;
    }

    xf86GPUScreens[i]->pScreen->CloseScreen(xf86GPUScreens[i]->pScreen);

    RemoveGPUScreen(xf86GPUScreens[i]->pScreen);
    xf86DeleteScreen(xf86GPUScreens[i]);

    xf86UnclaimPlatformSlot(&xf86_platform_devices[index], NULL);

    xf86_remove_platform_device(index);

    RRResourcesChanged(xf86Screens[0]->pScreen);
    RRTellChanged(xf86Screens[0]->pScreen);
 out:
    return;
}

/* called on return from VT switch to find any new devices */
void xf86platformVTProbe(void)
{
    int i;

    for (i = 0; i < xf86_num_platform_devices; i++) {
        if (!(xf86_platform_devices[i].flags & XF86_PDEV_UNOWNED))
            continue;

        xf86_platform_devices[i].flags &= ~XF86_PDEV_UNOWNED;
        xf86PlatformReprobeDevice(i, xf86_platform_devices[i].attribs);
    }
}

void xf86platformPrimary(void)
{
    /* use the first platform device as a fallback */
    if (primaryBus.type == BUS_NONE) {
        xf86Msg(X_INFO, "no primary bus or device found\n");

        if (xf86_num_platform_devices > 0) {
            primaryBus.id.plat = &xf86_platform_devices[0];
            primaryBus.type = BUS_PLATFORM;

            xf86Msg(X_NONE, "\tfalling back to %s\n", primaryBus.id.plat->attribs->syspath);
        }
    }
}
#endif
