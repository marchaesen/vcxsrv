/*
 * Copyright 2024 Sergio Lopez
 * SPDX-License-Identifier: MIT
 */

#include "agx_device_virtio.h"

#include <inttypes.h>
#include <sys/mman.h>

#include "drm-uapi/virtgpu_drm.h"
#include "unstable_asahi_drm.h"

#define VIRGL_RENDERER_UNSTABLE_APIS 1
#include "vdrm.h"
#include "virglrenderer_hw.h"

#include "asahi_proto.h"

/**
 * Helper for simple pass-thru ioctls
 */
int
agx_virtio_simple_ioctl(struct agx_device *dev, unsigned cmd, void *_req)
{
   struct vdrm_device *vdrm = dev->vdrm;
   unsigned req_len = sizeof(struct asahi_ccmd_ioctl_simple_req);
   unsigned rsp_len = sizeof(struct asahi_ccmd_ioctl_simple_rsp);

   req_len += _IOC_SIZE(cmd);
   if (cmd & IOC_OUT)
      rsp_len += _IOC_SIZE(cmd);

   uint8_t buf[req_len];
   struct asahi_ccmd_ioctl_simple_req *req = (void *)buf;
   struct asahi_ccmd_ioctl_simple_rsp *rsp;

   req->hdr = ASAHI_CCMD(IOCTL_SIMPLE, req_len);
   req->cmd = cmd;
   memcpy(req->payload, _req, _IOC_SIZE(cmd));

   rsp = vdrm_alloc_rsp(vdrm, &req->hdr, rsp_len);

   int ret = vdrm_send_req(vdrm, &req->hdr, true);
   if (ret) {
      fprintf(stderr, "simple_ioctl: vdrm_send_req failed\n");
      return ret;
   }

   if (cmd & IOC_OUT)
      memcpy(_req, rsp->payload, _IOC_SIZE(cmd));

   return rsp->ret;
}

static struct agx_bo *
agx_virtio_bo_alloc(struct agx_device *dev, size_t size, size_t align,
                    enum agx_bo_flags flags)
{
   struct agx_bo *bo;
   unsigned handle = 0;

   /* executable implies low va */
   assert(!(flags & AGX_BO_EXEC) || (flags & AGX_BO_LOW_VA));

   struct asahi_ccmd_gem_new_req req = {
      .hdr = ASAHI_CCMD(GEM_NEW, sizeof(req)),
      .size = size,
   };

   if (flags & AGX_BO_WRITEBACK)
      req.flags |= ASAHI_GEM_WRITEBACK;

   uint32_t blob_flags =
      VIRTGPU_BLOB_FLAG_USE_MAPPABLE | VIRTGPU_BLOB_FLAG_USE_SHAREABLE;

   req.bind_flags = ASAHI_BIND_READ;
   if (!(flags & AGX_BO_READONLY)) {
      req.bind_flags |= ASAHI_BIND_WRITE;
   }

   uint32_t blob_id = p_atomic_inc_return(&dev->next_blob_id);

   enum agx_va_flags va_flags = flags & AGX_BO_LOW_VA ? AGX_VA_USC : 0;
   struct agx_va *va = agx_va_alloc(dev, size, align, va_flags, 0);
   if (!va) {
      fprintf(stderr, "Failed to allocate BO VMA\n");
      return NULL;
   }

   /* Note: optional, can zero out for not mapping for sparse */
   req.addr = va->addr;
   req.blob_id = blob_id;
   req.vm_id = dev->vm_id;

   handle = vdrm_bo_create(dev->vdrm, size, blob_flags, blob_id, &req.hdr);
   if (!handle) {
      fprintf(stderr, "vdrm_bo_created failed\n");
      return NULL;
   }

   pthread_mutex_lock(&dev->bo_map_lock);
   bo = agx_lookup_bo(dev, handle);
   dev->max_handle = MAX2(dev->max_handle, handle);
   pthread_mutex_unlock(&dev->bo_map_lock);

   /* Fresh handle */
   assert(!memcmp(bo, &((struct agx_bo){}), sizeof(*bo)));

   bo->dev = dev;
   bo->size = size;
   bo->align = align;
   bo->flags = flags;
   bo->handle = handle;
   bo->prime_fd = -1;
   bo->blob_id = blob_id;
   bo->va = va;
   bo->vbo_res_id = vdrm_handle_to_res_id(dev->vdrm, handle);
   return bo;
}

static int
agx_virtio_bo_bind(struct agx_device *dev, struct agx_bo *bo, uint64_t addr,
                   size_t size_B, uint64_t offset_B, uint32_t flags,
                   bool unbind)
{
   struct asahi_ccmd_gem_bind_req req = {
      .hdr.cmd = ASAHI_CCMD_GEM_BIND,
      .hdr.len = sizeof(struct asahi_ccmd_gem_bind_req),
      .bind = {
         .op = unbind ? ASAHI_BIND_OP_UNBIND : ASAHI_BIND_OP_BIND,
         .flags = flags,
         .vm_id = dev->vm_id,
         .handle = bo->vbo_res_id,
         .offset = offset_B,
         .range = size_B,
         .addr = addr,
      }};

   int ret = vdrm_send_req(dev->vdrm, &req.hdr, false);
   if (ret) {
      fprintf(stderr, "ASAHI_CCMD_GEM_BIND failed: %d (handle=%d)\n", ret,
              bo->handle);
   }

   return ret;
}

static int
agx_virtio_bo_bind_object(struct agx_device *dev, struct agx_bo *bo,
                          uint32_t *object_handle, size_t size_B,
                          uint64_t offset_B, uint32_t flags)
{
   struct asahi_ccmd_gem_bind_object_req req = {
      .hdr.cmd = ASAHI_CCMD_GEM_BIND_OBJECT,
      .hdr.len = sizeof(struct asahi_ccmd_gem_bind_object_req),
      .bind = {
         .op = ASAHI_BIND_OBJECT_OP_BIND,
         .flags = flags,
         .vm_id = 0,
         .handle = bo->vbo_res_id,
         .offset = offset_B,
         .range = size_B,
      }};

   struct asahi_ccmd_gem_bind_object_rsp *rsp;

   rsp = vdrm_alloc_rsp(dev->vdrm, &req.hdr,
                        sizeof(struct asahi_ccmd_gem_bind_object_rsp));

   int ret = vdrm_send_req(dev->vdrm, &req.hdr, true);
   if (ret || rsp->ret) {
      fprintf(stderr,
              "ASAHI_CCMD_GEM_BIND_OBJECT bind failed: %d:%d (handle=%d)\n",
              ret, rsp->ret, bo->handle);
   }

   if (!rsp->ret)
      *object_handle = rsp->object_handle;

   return rsp->ret;
}

static int
agx_virtio_bo_unbind_object(struct agx_device *dev, uint32_t object_handle,
                            uint32_t flags)
{
   struct asahi_ccmd_gem_bind_object_req req = {
      .hdr.cmd = ASAHI_CCMD_GEM_BIND_OBJECT,
      .hdr.len = sizeof(struct asahi_ccmd_gem_bind_object_req),
      .bind = {
         .op = ASAHI_BIND_OBJECT_OP_UNBIND,
         .flags = flags,
         .object_handle = object_handle,
      }};

   int ret = vdrm_send_req(dev->vdrm, &req.hdr, false);
   if (ret) {
      fprintf(stderr,
              "ASAHI_CCMD_GEM_BIND_OBJECT unbind failed: %d (handle=%d)\n", ret,
              object_handle);
   }

   return 0;
}

static void
agx_virtio_bo_mmap(struct agx_device *dev, struct agx_bo *bo)
{
   bo->_map = vdrm_bo_map(dev->vdrm, bo->handle, bo->size, NULL);
   if (bo->_map == MAP_FAILED) {
      bo->_map = NULL;
      fprintf(stderr, "mmap failed: result=%p size=0x%llx fd=%i\n", bo->_map,
              (long long)bo->size, dev->fd);
   }
}

static ssize_t
agx_virtio_get_params(struct agx_device *dev, void *buf, size_t size)
{
   struct vdrm_device *vdrm = dev->vdrm;
   struct asahi_ccmd_get_params_req req = {
      .params.size = size,
      .hdr.cmd = ASAHI_CCMD_GET_PARAMS,
      .hdr.len = sizeof(struct asahi_ccmd_get_params_req),
   };
   struct asahi_ccmd_get_params_rsp *rsp;

   rsp = vdrm_alloc_rsp(vdrm, &req.hdr,
                        sizeof(struct asahi_ccmd_get_params_rsp) + size);

   int ret = vdrm_send_req(vdrm, &req.hdr, true);
   if (ret)
      goto out;

   if (rsp->virt_uabi_version != ASAHI_PROTO_UNSTABLE_UABI_VERSION) {
      fprintf(stderr, "Virt UABI mismatch: Host %d, Mesa %d\n",
              rsp->virt_uabi_version, ASAHI_PROTO_UNSTABLE_UABI_VERSION);
      return -1;
   }

   ret = rsp->ret;
   if (!ret) {
      memcpy(buf, &rsp->payload, size);
      return size;
   }

out:
   return ret;
}

static void
agx_virtio_serialize_attachments(char **ptr, uint64_t attachments,
                                 uint32_t count)
{
   if (!count)
      return;

   size_t attachments_size = sizeof(struct drm_asahi_attachment) * count;
   memcpy(*ptr, (char *)(uintptr_t)attachments, attachments_size);
   *ptr += attachments_size;
}

static int
agx_virtio_submit(struct agx_device *dev, struct drm_asahi_submit *submit,
                  struct agx_submit_virt *virt)
{
   struct drm_asahi_command *commands =
      (struct drm_asahi_command *)(uintptr_t)submit->commands;
   struct drm_asahi_sync *in_syncs =
      (struct drm_asahi_sync *)(uintptr_t)submit->in_syncs;
   struct drm_asahi_sync *out_syncs =
      (struct drm_asahi_sync *)(uintptr_t)submit->out_syncs;
   size_t req_len = sizeof(struct asahi_ccmd_submit_req);

   for (int i = 0; i < submit->command_count; i++) {
      switch (commands[i].cmd_type) {
      case DRM_ASAHI_CMD_COMPUTE: {
         struct drm_asahi_cmd_compute *compute =
            (struct drm_asahi_cmd_compute *)(uintptr_t)commands[i].cmd_buffer;
         req_len += sizeof(struct drm_asahi_command) +
                    sizeof(struct drm_asahi_cmd_compute);
         req_len +=
            compute->attachment_count * sizeof(struct drm_asahi_attachment);

         if (compute->extensions) {
            assert(*(uint32_t *)(uintptr_t)compute->extensions ==
                   ASAHI_COMPUTE_EXT_TIMESTAMPS);
            req_len += sizeof(struct drm_asahi_cmd_compute_user_timestamps);
         }
         break;
      }

      case DRM_ASAHI_CMD_RENDER: {
         struct drm_asahi_cmd_render *render =
            (struct drm_asahi_cmd_render *)(uintptr_t)commands[i].cmd_buffer;
         req_len += sizeof(struct drm_asahi_command) +
                    sizeof(struct drm_asahi_cmd_render);
         req_len += render->fragment_attachment_count *
                    sizeof(struct drm_asahi_attachment);
         req_len += render->vertex_attachment_count *
                    sizeof(struct drm_asahi_attachment);

         if (render->extensions) {
            assert(*(uint32_t *)(uintptr_t)render->extensions ==
                   ASAHI_RENDER_EXT_TIMESTAMPS);
            req_len += sizeof(struct drm_asahi_cmd_render_user_timestamps);
         }
         break;
      }

      default:
         return EINVAL;
      }
   }

   size_t extres_size =
      sizeof(struct asahi_ccmd_submit_res) * virt->extres_count;
   req_len += extres_size;

   struct asahi_ccmd_submit_req *req =
      (struct asahi_ccmd_submit_req *)calloc(1, req_len);

   req->queue_id = submit->queue_id;
   req->result_res_id = virt->vbo_res_id;
   req->command_count = submit->command_count;
   req->extres_count = virt->extres_count;

   char *ptr = (char *)&req->payload;

   for (int i = 0; i < submit->command_count; i++) {
      memcpy(ptr, &commands[i], sizeof(struct drm_asahi_command));
      ptr += sizeof(struct drm_asahi_command);

      memcpy(ptr, (char *)(uintptr_t)commands[i].cmd_buffer,
             commands[i].cmd_buffer_size);
      ptr += commands[i].cmd_buffer_size;

      switch (commands[i].cmd_type) {
      case DRM_ASAHI_CMD_RENDER: {
         struct drm_asahi_cmd_render *render =
            (struct drm_asahi_cmd_render *)(uintptr_t)commands[i].cmd_buffer;
         agx_virtio_serialize_attachments(&ptr, render->vertex_attachments,
                                          render->vertex_attachment_count);
         agx_virtio_serialize_attachments(&ptr, render->fragment_attachments,
                                          render->fragment_attachment_count);
         if (render->extensions) {
            struct drm_asahi_cmd_render_user_timestamps *ext =
               (struct drm_asahi_cmd_render_user_timestamps *)(uintptr_t)
                  render->extensions;
            assert(!ext->next);
            memcpy(ptr, (void *)ext, sizeof(*ext));
            ptr += sizeof(*ext);
         }
         break;
      }
      case DRM_ASAHI_CMD_COMPUTE: {
         struct drm_asahi_cmd_compute *compute =
            (struct drm_asahi_cmd_compute *)(uintptr_t)commands[i].cmd_buffer;
         agx_virtio_serialize_attachments(&ptr, compute->attachments,
                                          compute->attachment_count);
         if (compute->extensions) {
            struct drm_asahi_cmd_compute_user_timestamps *ext =
               (struct drm_asahi_cmd_compute_user_timestamps *)(uintptr_t)
                  compute->extensions;
            assert(!ext->next);
            memcpy(ptr, (void *)ext, sizeof(*ext));
            ptr += sizeof(*ext);
         }
         break;
      }
      }
   }

   memcpy(ptr, virt->extres, extres_size);
   ptr += extres_size;

   req->hdr.cmd = ASAHI_CCMD_SUBMIT;
   req->hdr.len = req_len;

   struct drm_virtgpu_execbuffer_syncobj *vdrm_in_syncs = calloc(
      submit->in_sync_count, sizeof(struct drm_virtgpu_execbuffer_syncobj));
   for (int i = 0; i < submit->in_sync_count; i++) {
      vdrm_in_syncs[i].handle = in_syncs[i].handle;
      vdrm_in_syncs[i].point = in_syncs[i].timeline_value;
   }

   struct drm_virtgpu_execbuffer_syncobj *vdrm_out_syncs = calloc(
      submit->out_sync_count, sizeof(struct drm_virtgpu_execbuffer_syncobj));
   for (int i = 0; i < submit->out_sync_count; i++) {
      vdrm_out_syncs[i].handle = out_syncs[i].handle;
      vdrm_out_syncs[i].point = out_syncs[i].timeline_value;
   }

   struct vdrm_execbuf_params p = {
      /* Signal the host we want to wait for the command to complete */
      .ring_idx = 1,
      .req = &req->hdr,
      .num_in_syncobjs = submit->in_sync_count,
      .in_syncobjs = vdrm_in_syncs,
      .num_out_syncobjs = submit->out_sync_count,
      .out_syncobjs = vdrm_out_syncs,
   };

   int ret = vdrm_execbuf(dev->vdrm, &p);

   free(vdrm_out_syncs);
   free(vdrm_in_syncs);
   free(req);
   return ret;
}

const agx_device_ops_t agx_virtio_device_ops = {
   .bo_alloc = agx_virtio_bo_alloc,
   .bo_bind = agx_virtio_bo_bind,
   .bo_mmap = agx_virtio_bo_mmap,
   .get_params = agx_virtio_get_params,
   .submit = agx_virtio_submit,
   .bo_bind_object = agx_virtio_bo_bind_object,
   .bo_unbind_object = agx_virtio_bo_unbind_object,
};

bool
agx_virtio_open_device(struct agx_device *dev)
{
   struct vdrm_device *vdrm;

   vdrm = vdrm_device_connect(dev->fd, 2);
   if (!vdrm) {
      fprintf(stderr, "could not connect vdrm\n");
      return false;
   }

   dev->vdrm = vdrm;
   dev->ops = agx_virtio_device_ops;
   return true;
}
