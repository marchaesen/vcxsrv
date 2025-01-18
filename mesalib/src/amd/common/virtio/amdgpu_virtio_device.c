/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "amdgpu_virtio_private.h"

#include "util/bitscan.h"
#include "util/log.h"
#include "util/os_file.h"
#include "util/u_debug.h"

#include <xf86drm.h>

/* amdvgpu_device manage the virtual GPU.
 *
 * It owns a vdrm_device instance, the rings and manage seqno.
 * Since it's a drop-in replacement for libdrm_amdgpu's amdgpu_device,
 * it follows its behavior: if the same device is opened multiple times,
 * the same amdvgpu_device will be used.
 */
static simple_mtx_t dev_mutex = SIMPLE_MTX_INITIALIZER;
static amdvgpu_device_handle dev_list;

static int fd_compare(int fd1, int fd2)
{
   char *name1 = drmGetPrimaryDeviceNameFromFd(fd1);
   char *name2 = drmGetPrimaryDeviceNameFromFd(fd2);
   int result;

   if (name1 == NULL || name2 == NULL) {
      free(name1);
      free(name2);
      return 0;
   }

   result = strcmp(name1, name2);
   free(name1);
   free(name2);

   return result;
}

static void amdvgpu_device_reference(struct amdvgpu_device **dst,
                                     struct amdvgpu_device *src)
{
   if (update_references(*dst ? &(*dst)->refcount : NULL,
                         src ? &src->refcount : NULL)) {
      struct amdvgpu_device *dev, *prev = NULL;
      for (dev = dev_list; dev; dev = dev->next) {
         if (dev == (*dst)) {
            if (prev == NULL)
               dev_list = dev->next;
            else
               prev->next = dev->next;
            break;
         }
         prev = dev;
      }

      dev = *dst;

      /* Destroy BOs before closing vdrm */
      hash_table_foreach(dev->handle_to_vbo, entry) {
         struct amdvgpu_bo *bo = entry->data;
         amdvgpu_bo_free(dev, bo);
      }
      _mesa_hash_table_destroy(dev->handle_to_vbo, NULL);
      /* Destroy contextx. */
      hash_table_foreach(&dev->contexts, entry)
         amdvgpu_cs_ctx_free(dev, (uint32_t)(uintptr_t)entry->key);
      _mesa_hash_table_clear(&dev->contexts, NULL);

      simple_mtx_destroy(&dev->handle_to_vbo_mutex);
      simple_mtx_destroy(&dev->contexts_mutex);

      amdgpu_va_manager_deinit(dev->va_mgr);

      vdrm_device_close(dev->vdev);

      close(dev->fd);
      free(dev);
   }

   *dst = src;
}

int amdvgpu_device_deinitialize(amdvgpu_device_handle dev) {
   simple_mtx_lock(&dev_mutex);
   amdvgpu_device_reference(&dev, NULL);
   simple_mtx_unlock(&dev_mutex);
   return 0;
}

int amdvgpu_device_initialize(int fd, uint32_t *drm_major, uint32_t *drm_minor,
                              amdvgpu_device_handle* dev_out) {
   simple_mtx_lock(&dev_mutex);
   amdvgpu_device_handle dev;

   for (dev = dev_list; dev; dev = dev->next)
      if (fd_compare(dev->fd, fd) == 0)
         break;

   if (dev) {
      *dev_out = NULL;
      amdvgpu_device_reference(dev_out, dev);
      *drm_major = dev->vdev->caps.version_major;
      *drm_minor = dev->vdev->caps.version_minor;
      simple_mtx_unlock(&dev_mutex);
      return 0;
   }

   /* fd is owned by the amdgpu_screen_winsys that called this function.
    * amdgpu_screen_winsys' lifetime may be shorter than the device's one,
    * so dup fd to tie its lifetime to the device's one.
    */
   fd = os_dupfd_cloexec(fd);

   struct vdrm_device *vdev = vdrm_device_connect(fd, VIRTGPU_DRM_CONTEXT_AMDGPU);
   if (vdev == NULL) {
      mesa_loge("vdrm_device_connect failed\n");
      simple_mtx_unlock(&dev_mutex);
      return -1;
   }

   dev = calloc(1, sizeof(struct amdvgpu_device));
   dev->refcount = 1;
   dev->next = dev_list;
   dev_list = dev;
   dev->fd = fd;
   dev->vdev = vdev;

   simple_mtx_init(&dev->handle_to_vbo_mutex, mtx_plain);
   simple_mtx_init(&dev->contexts_mutex, mtx_plain);

   dev->handle_to_vbo = _mesa_hash_table_create_u32_keys(NULL);

   p_atomic_set(&dev->next_blob_id, 1);

   *dev_out = dev;

   simple_mtx_unlock(&dev_mutex);

   struct drm_amdgpu_info info;
   info.return_pointer = (uintptr_t)&dev->dev_info;
   info.query = AMDGPU_INFO_DEV_INFO;
   info.return_size = sizeof(dev->dev_info);
   int r = amdvgpu_query_info(dev, &info);
   assert(r == 0);

   /* Ring idx 0 is reserved for commands running on CPU. */
   unsigned next_ring_idx = 1;
   for (unsigned i = 0; i < AMD_NUM_IP_TYPES; ++i) {
      struct drm_amdgpu_info_hw_ip ip_info = {0};
      struct drm_amdgpu_info request = {0};
      request.return_pointer = (uintptr_t)&ip_info;
      request.return_size = sizeof(ip_info);
      request.query = AMDGPU_INFO_HW_IP_INFO;
      request.query_hw_ip.type = i;
      request.query_hw_ip.ip_instance = 0;
      r = amdvgpu_query_info(dev, &request);
      if (r == 0 && ip_info.available_rings) {
         int count = util_bitcount(ip_info.available_rings);
         dev->virtio_ring_mapping[i] = next_ring_idx;
         next_ring_idx += count;
      }
   }
   /* VIRTGPU_CONTEXT_PARAM_NUM_RINGS is hardcoded for now. */
   assert(next_ring_idx <= 64);
   dev->num_virtio_rings = next_ring_idx - 1;

   dev->va_mgr = amdgpu_va_manager_alloc();
   amdgpu_va_manager_init(dev->va_mgr,
      dev->dev_info.virtual_address_offset, dev->dev_info.virtual_address_max,
      dev->dev_info.high_va_offset, dev->dev_info.high_va_max,
      dev->dev_info.virtual_address_alignment);

   _mesa_hash_table_init(&dev->contexts, NULL,
                         _mesa_hash_pointer, _mesa_key_pointer_equal);
   dev->allow_multiple_amdgpu_ctx = debug_get_bool_option("MULTIPLE_AMDGPU_CTX", false);
   dev->sync_cmd = debug_get_num_option("VIRTIO_SYNC_CMD", 0);

   *drm_major = dev->vdev->caps.version_major;
   *drm_minor = dev->vdev->caps.version_minor;

   return 0;
}
