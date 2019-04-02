/*
 * Copyright © 2018 Google, Inc.
 * Copyright © 2015 Intel Corporation
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

#include "tu_private.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <xf86drm.h>

#include "drm/msm_drm.h"

static int
tu_drm_get_param(const struct tu_physical_device *dev,
                 uint32_t param,
                 uint64_t *value)
{
   /* Technically this requires a pipe, but the kernel only supports one pipe
    * anyway at the time of writing and most of these are clearly pipe
    * independent. */
   struct drm_msm_param req = {
      .pipe = MSM_PIPE_3D0,
      .param = param,
   };

   int ret = drmCommandWriteRead(dev->local_fd, DRM_MSM_GET_PARAM, &req,
                                 sizeof(req));
   if (ret)
      return ret;

   *value = req.value;

   return 0;
}

int
tu_drm_get_gpu_id(const struct tu_physical_device *dev, uint32_t *id)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev, MSM_PARAM_GPU_ID, &value);
   if (ret)
      return ret;

   *id = value;
   return 0;
}

int
tu_drm_get_gmem_size(const struct tu_physical_device *dev, uint32_t *size)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev, MSM_PARAM_GMEM_SIZE, &value);
   if (ret)
      return ret;

   *size = value;
   return 0;
}

int
tu_drm_submitqueue_new(const struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id)
{
   struct drm_msm_submitqueue req = {
      .flags = 0,
      .prio = priority,
   };

   int ret = drmCommandWriteRead(dev->physical_device->local_fd,
                                 DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
   if (ret)
      return ret;

   *queue_id = req.id;
   return 0;
}

void
tu_drm_submitqueue_close(const struct tu_device *dev, uint32_t queue_id)
{
   drmCommandWrite(dev->physical_device->local_fd, DRM_MSM_SUBMITQUEUE_CLOSE,
                   &queue_id, sizeof(uint32_t));
}

/**
 * Return gem handle on success. Return 0 on failure.
 */
uint32_t
tu_gem_new(const struct tu_device *dev, uint64_t size, uint32_t flags)
{
   struct drm_msm_gem_new req = {
      .size = size,
      .flags = flags,
   };

   int ret = drmCommandWriteRead(dev->physical_device->local_fd,
                                 DRM_MSM_GEM_NEW, &req, sizeof(req));
   if (ret)
      return 0;

   return req.handle;
}

uint32_t
tu_gem_import_dmabuf(const struct tu_device *dev, int prime_fd, uint64_t size)
{
   /* lseek() to get the real size */
   off_t real_size = lseek(prime_fd, 0, SEEK_END);
   lseek(prime_fd, 0, SEEK_SET);
   if (real_size < 0 || (uint64_t) real_size < size)
      return 0;

   uint32_t gem_handle;
   int ret = drmPrimeFDToHandle(dev->physical_device->local_fd, prime_fd,
                                &gem_handle);
   if (ret)
      return 0;

   return gem_handle;
}

int
tu_gem_export_dmabuf(const struct tu_device *dev, uint32_t gem_handle)
{
   int prime_fd;
   int ret = drmPrimeHandleToFD(dev->physical_device->local_fd, gem_handle,
                                DRM_CLOEXEC, &prime_fd);

   return ret == 0 ? prime_fd : -1;
}

void
tu_gem_close(const struct tu_device *dev, uint32_t gem_handle)
{
   struct drm_gem_close req = {
      .handle = gem_handle,
   };

   drmIoctl(dev->physical_device->local_fd, DRM_IOCTL_GEM_CLOSE, &req);
}

/** Return UINT64_MAX on error. */
static uint64_t
tu_gem_info(const struct tu_device *dev, uint32_t gem_handle, uint32_t info)
{
   struct drm_msm_gem_info req = {
      .handle = gem_handle,
      .info = info,
   };

   int ret = drmCommandWriteRead(dev->physical_device->local_fd,
                                 DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret == -1)
      return UINT64_MAX;

   return req.value;
}

/** Return UINT64_MAX on error. */
uint64_t
tu_gem_info_offset(const struct tu_device *dev, uint32_t gem_handle)
{
   return tu_gem_info(dev, gem_handle, MSM_INFO_GET_OFFSET);
}

/** Return UINT64_MAX on error. */
uint64_t
tu_gem_info_iova(const struct tu_device *dev, uint32_t gem_handle)
{
   return tu_gem_info(dev, gem_handle, MSM_INFO_GET_IOVA);
}
