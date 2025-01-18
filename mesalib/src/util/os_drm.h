/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/* These are identical to libdrm functions drmCommandWrite* and drmIoctl,
 * but unlike libdrm, these are inlinable.
 */

#ifndef OS_DRM_H
#define OS_DRM_H

#ifdef _WIN32
#error "Windows shouldn't include this."
#endif

#include <sys/ioctl.h>
#include <errno.h>
#include <xf86drm.h>

static inline int
drm_ioctl(int fd, uint32_t request, void *arg)
{
   int ret;

   do {
      ret = ioctl(fd, request, arg);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret ? -errno : 0;
}

static inline int
drm_ioctl_write(int fd, unsigned drm_command_index, void *data, unsigned size)
{
   uint32_t request = DRM_IOC(DRM_IOC_WRITE, DRM_IOCTL_BASE,
                              DRM_COMMAND_BASE + drm_command_index, size);
   return drm_ioctl(fd, request, data);
}

static inline int
drm_ioctl_write_read(int fd, unsigned drm_command_index, void *data,
                     unsigned size)
{
   uint32_t request = DRM_IOC(DRM_IOC_READ | DRM_IOC_WRITE, DRM_IOCTL_BASE,
                              DRM_COMMAND_BASE + drm_command_index, size);
   return drm_ioctl(fd, request, data);
}

#endif
