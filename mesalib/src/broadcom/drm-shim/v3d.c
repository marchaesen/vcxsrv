/*
 * Copyright © 2018 Broadcom
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
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include "drm-uapi/v3d_drm.h"
#include "drm-shim/drm_shim.h"
#include "v3d.h"
#include "v3d_simulator_wrapper.h"

bool drm_shim_driver_prefers_first_render_node = false;

static struct v3d_device_info devinfo;
struct v3d_shim_device v3d = {
        .devinfo = &devinfo
};

struct v3d_bo *v3d_bo_lookup(struct shim_fd *shim_fd, int handle)
{
        return v3d_bo(drm_shim_bo_lookup(shim_fd, handle));
}

int
v3d_ioctl_wait_bo(int fd, unsigned long request, void *arg)
{
        /* No need to wait on anything yet, given that we submit
         * synchronously.
         */
        return 0;
}

int
v3d_ioctl_mmap_bo(int fd, unsigned long request, void *arg)
{
        struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
        struct drm_v3d_mmap_bo *map = arg;
        struct shim_bo *bo = drm_shim_bo_lookup(shim_fd, map->handle);

        map->offset = drm_shim_bo_get_mmap_offset(shim_fd, bo);

        drm_shim_bo_put(bo);

        return 0;
}

int
v3d_ioctl_get_bo_offset(int fd, unsigned long request, void *arg)
{
        struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
        struct drm_v3d_get_bo_offset *get = arg;
        struct v3d_bo *bo = v3d_bo_lookup(shim_fd, get->handle);

        get->offset = bo->offset;

        drm_shim_bo_put(&bo->base);

        return 0;
}

void
drm_shim_driver_init(void)
{
        shim_device.bus_type = DRM_BUS_PLATFORM;
        shim_device.driver_name = "v3d";

        drm_shim_override_file("OF_FULLNAME=/rdb/v3d\n"
                               "OF_COMPATIBLE_N=1\n"
                               "OF_COMPATIBLE_0=brcm,7278-v3d\n",
                               "/sys/dev/char/%d:%d/device/uevent",
                               DRM_MAJOR, render_node_minor);

        v3d.hw = v3d_hw_auto_new(NULL);
        v3d.devinfo->ver = v3d_hw_get_version(v3d.hw);

        if (v3d.devinfo->ver >= 42)
                v3d42_drm_shim_driver_init();
        else if (v3d.devinfo->ver >= 41)
                v3d41_drm_shim_driver_init();
        else
                v3d33_drm_shim_driver_init();
}
