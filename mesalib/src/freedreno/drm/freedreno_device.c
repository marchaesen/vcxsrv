/*
 * Copyright (C) 2012-2018 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util/os_file.h"

#include "freedreno_drmif.h"
#include "freedreno_priv.h"

struct fd_device *msm_device_new(int fd, drmVersionPtr version);
#if HAVE_FREEDRENO_VIRTIO
struct fd_device *virtio_device_new(int fd, drmVersionPtr version);
#endif

struct fd_device *
fd_device_new(int fd)
{
   struct fd_device *dev = NULL;
   drmVersionPtr version;

   /* figure out if we are kgsl or msm drm driver: */
   version = drmGetVersion(fd);
   if (!version) {
      ERROR_MSG("cannot get version: %s", strerror(errno));
      return NULL;
   }

   if (!strcmp(version->name, "msm")) {
      DEBUG_MSG("msm DRM device");
      if (version->version_major != 1) {
         ERROR_MSG("unsupported version: %u.%u.%u", version->version_major,
                   version->version_minor, version->version_patchlevel);
         goto out;
      }

      dev = msm_device_new(fd, version);
#if HAVE_FREEDRENO_VIRTIO
   } else if (!strcmp(version->name, "virtio_gpu")) {
      DEBUG_MSG("virtio_gpu DRM device");
      dev = virtio_device_new(fd, version);
#endif
#if HAVE_FREEDRENO_KGSL
   } else if (!strcmp(version->name, "kgsl")) {
      DEBUG_MSG("kgsl DRM device");
      dev = kgsl_device_new(fd);
#endif
   }

   if (!dev) {
      INFO_MSG("unsupported device: %s", version->name);
      goto out;
   }

out:
   drmFreeVersion(version);

   if (!dev)
      return NULL;

   p_atomic_set(&dev->refcnt, 1);
   dev->fd = fd;
   dev->handle_table =
      _mesa_hash_table_create(NULL, _mesa_hash_u32, _mesa_key_u32_equal);
   dev->name_table =
      _mesa_hash_table_create(NULL, _mesa_hash_u32, _mesa_key_u32_equal);
   fd_bo_cache_init(&dev->bo_cache, false);
   fd_bo_cache_init(&dev->ring_cache, true);

   list_inithead(&dev->deferred_submits);
   simple_mtx_init(&dev->submit_lock, mtx_plain);
   simple_mtx_init(&dev->suballoc_lock, mtx_plain);

   return dev;
}

/* like fd_device_new() but creates it's own private dup() of the fd
 * which is close()d when the device is finalized.
 */
struct fd_device *
fd_device_new_dup(int fd)
{
   int dup_fd = os_dupfd_cloexec(fd);
   struct fd_device *dev = fd_device_new(dup_fd);
   if (dev)
      dev->closefd = 1;
   else
      close(dup_fd);
   return dev;
}

/* Convenience helper to open the drm device and return new fd_device:
 */
struct fd_device *
fd_device_open(void)
{
   int fd;

   fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
#if HAVE_FREEDRENO_VIRTIO
   if (fd < 0)
      fd = drmOpenWithType("virtio_gpu", NULL, DRM_NODE_RENDER);
#endif
   if (fd < 0)
      return NULL;

   return fd_device_new(fd);
}

struct fd_device *
fd_device_ref(struct fd_device *dev)
{
   p_atomic_inc(&dev->refcnt);
   return dev;
}

void
fd_device_purge(struct fd_device *dev)
{
   simple_mtx_lock(&table_lock);
   fd_bo_cache_cleanup(&dev->bo_cache, 0);
   fd_bo_cache_cleanup(&dev->ring_cache, 0);
   simple_mtx_unlock(&table_lock);
}

static void
fd_device_del_impl(struct fd_device *dev)
{
   simple_mtx_assert_locked(&table_lock);

   assert(list_is_empty(&dev->deferred_submits));

   if (dev->suballoc_bo)
      fd_bo_del_locked(dev->suballoc_bo);

   fd_bo_cache_cleanup(&dev->bo_cache, 0);
   fd_bo_cache_cleanup(&dev->ring_cache, 0);

   /* Needs to be after bo cache cleanup in case backend has a
    * util_vma_heap that it destroys:
    */
   dev->funcs->destroy(dev);

   _mesa_hash_table_destroy(dev->handle_table, NULL);
   _mesa_hash_table_destroy(dev->name_table, NULL);

   if (util_queue_is_initialized(&dev->submit_queue))
      util_queue_destroy(&dev->submit_queue);

   if (dev->closefd)
      close(dev->fd);

   free(dev);
}

void
fd_device_del_locked(struct fd_device *dev)
{
   if (!p_atomic_dec_zero(&dev->refcnt))
      return;
   fd_device_del_impl(dev);
}

void
fd_device_del(struct fd_device *dev)
{
   if (!p_atomic_dec_zero(&dev->refcnt))
      return;
   simple_mtx_lock(&table_lock);
   fd_device_del_impl(dev);
   simple_mtx_unlock(&table_lock);
}

int
fd_device_fd(struct fd_device *dev)
{
   return dev->fd;
}

enum fd_version
fd_device_version(struct fd_device *dev)
{
   return dev->version;
}

DEBUG_GET_ONCE_BOOL_OPTION(libgl, "LIBGL_DEBUG", false)

bool
fd_dbg(void)
{
   return debug_get_option_libgl();
}

bool
fd_has_syncobj(struct fd_device *dev)
{
   uint64_t value;
   if (drmGetCap(dev->fd, DRM_CAP_SYNCOBJ, &value))
      return false;
   return value && dev->version >= FD_VERSION_FENCE_FD;
}
