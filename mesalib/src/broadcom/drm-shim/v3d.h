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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef DRM_SHIM_V3D_H
#define DRM_SHIM_V3D_H

#include "broadcom/common/v3d_device_info.h"
#include "util/vma.h"

struct drm_shim_fd;

struct v3d_shim_device {
        struct v3d_hw *hw;
        struct v3d_device_info *devinfo;

        /* Base virtual address of the heap. */
        void *mem;
        /* Base hardware address of the heap. */
        uint32_t mem_base;
        /* Size of the heap. */
        size_t mem_size;

        /* Allocator for the GPU virtual addresses. */
        struct util_vma_heap heap;
};
extern struct v3d_shim_device v3d;

struct v3d_bo {
        struct shim_bo base;
        uint64_t offset;
        void *sim_vaddr;
        void *gem_vaddr;
};

static inline struct v3d_bo *
v3d_bo(struct shim_bo *bo)
{
        return (struct v3d_bo *)bo;
}

struct v3d_bo *v3d_bo_lookup(struct shim_fd *shim_fd, int handle);
int v3d_ioctl_wait_bo(int fd, unsigned long request, void *arg);
int v3d_ioctl_mmap_bo(int fd, unsigned long request, void *arg);
int v3d_ioctl_get_bo_offset(int fd, unsigned long request, void *arg);

void v3d33_drm_shim_driver_init(void);
void v3d41_drm_shim_driver_init(void);
void v3d42_drm_shim_driver_init(void);

#endif /* DRM_SHIM_V3D_H */
