/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AMDGPU_VIRTIO_PRIVATE_H
#define AMDGPU_VIRTIO_PRIVATE_H

#include "drm-uapi/amdgpu_drm.h"
#include "drm-uapi/virtgpu_drm.h"

#include "util/hash_table.h"
#include "util/simple_mtx.h"

#include "amd_family.h"

#include "virtio/vdrm/vdrm.h"
#include "virtio/virtio-gpu/drm_hw.h"
#include "amdgpu_virtio_proto.h"
#include "amdgpu_virtio.h"

struct amdvgpu_host_blob;
struct amdvgpu_host_blob_allocator;

/* Host context seqno handling.
 * seqno are monotonically increasing integer, so we don't need
 * to actually submit to know the value. This allows to not
 * wait for the submission to go to the host (= no need to wait
 * in the guest) and to know the seqno (= so we can take advantage
 * of user fence).
 */
struct amdvgpu_context {
   uint32_t refcount;
   uint32_t host_context_id;
   uint64_t ring_next_seqno[];
};

struct amdvgpu_device {
   struct vdrm_device * vdev;

   /* List of existing devices */
   int refcount;
   struct amdvgpu_device *next;

   int fd;

   /* Table mapping kms handles to amdvgpu_bo instances.
    * Used to maintain a 1-to-1 mapping between the 2.
    */
   simple_mtx_t handle_to_vbo_mutex;
   struct hash_table *handle_to_vbo;

   /* Submission through virtio-gpu are ring based.
    * Ring 0 is used for CPU jobs, then N rings are allocated: 1
    * per IP type per instance (so if the GPU has 1 gfx queue and 2
    * queues -> ring0 + 3 hw rings = 4 rings total).
    */
   uint32_t num_virtio_rings;
   uint32_t virtio_ring_mapping[AMD_NUM_IP_TYPES];

   struct drm_amdgpu_info_device dev_info;

   /* Blob id are per drm_file identifiers of host blobs.
    * Use a monotically increased integer to assign the blob id.
    */
   uint32_t next_blob_id;

   /* GPU VA management (allocation / release). */
   amdgpu_va_manager_handle va_mgr;

   /* Debug option to make some protocol commands synchronous.
    * If bit N is set, then the specific command will be sync.
    */
   int64_t sync_cmd;

   /* virtio-gpu uses a single context per drm_file and expects that
    * any 2 jobs submitted to the same {context, ring} will execute in
    * order.
    * amdgpu on the other hand allows for multiple context per drm_file,
    * so we either have to open multiple virtio-gpu drm_file to be able to
    * have 1 virtio-gpu context per amdgpu-context or use a single amdgpu
    * context.
    * Using multiple drm_file might cause BO sharing issues so for now limit
    * ourselves to a single amdgpu context. Each amdgpu_ctx object can schedule
    * parallel work on 1 gfx, 2 sdma, 4 compute, 1 of each vcn queue.
    */
   simple_mtx_t contexts_mutex;
   struct hash_table contexts;
   bool allow_multiple_amdgpu_ctx;
};

/* Refcounting helpers. Returns true when dst reaches 0. */
static inline bool update_references(int *dst, int *src)
{
   if (dst != src) {
      /* bump src first */
      if (src) {
         assert(p_atomic_read(src) > 0);
         p_atomic_inc(src);
      }
      if (dst) {
         return p_atomic_dec_zero(dst);
      }
   }
   return false;
}

#define virtio_ioctl(fd, name, args) ({                              \
      int ret = drmIoctl((fd), DRM_IOCTL_ ## name, (args));          \
      ret;                                                           \
      })

struct amdvgpu_host_blob_creation_params {
   struct drm_virtgpu_resource_create_blob args;
   struct amdgpu_ccmd_gem_new_req req;
};

struct amdvgpu_bo {
   struct amdvgpu_device *dev;

   /* Importing the same kms handle must return the same
    * amdvgpu_pointer, so we need a refcount.
    */
   int refcount;

   /* The size of the BO (might be smaller that the host
    * bo' size).
    */
   unsigned size;

   /* The host blob backing this bo. */
   struct amdvgpu_host_blob *host_blob;
};


uint32_t amdvgpu_get_resource_id(amdvgpu_bo_handle bo);

/* There are 2 return-code:
 *    - the virtio one, returned by vdrm_send_req
 *    - the host one, which only makes sense for sync
 *      requests.
 */
static inline
int vdrm_send_req_wrapper(amdvgpu_device_handle dev,
                          struct vdrm_ccmd_req *req,
                          struct amdgpu_ccmd_rsp *rsp,
                          bool sync) {
   if (dev->sync_cmd & (1u << req->cmd))
      sync = true;

   int r = vdrm_send_req(dev->vdev, req, sync);

   if (r)
      return r;

   if (sync)
      return rsp->ret;

   return 0;
}
#endif /* AMDGPU_VIRTIO_PRIVATE_H */
