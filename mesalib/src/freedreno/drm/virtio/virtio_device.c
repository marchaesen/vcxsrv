/*
 * Copyright © 2022 Google, Inc.
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
 */

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util/libsync.h"
#include "util/u_process.h"

#include "virtio_priv.h"

static void
virtio_device_destroy(struct fd_device *dev)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);

   fd_bo_del_locked(virtio_dev->shmem_bo);
   util_vma_heap_finish(&virtio_dev->address_space);
}

static const struct fd_device_funcs funcs = {
   .bo_new = virtio_bo_new,
   .bo_from_handle = virtio_bo_from_handle,
   .pipe_new = virtio_pipe_new,
   .destroy = virtio_device_destroy,
};

static int
get_capset(int fd, struct virgl_renderer_capset_drm *caps)
{
   struct drm_virtgpu_get_caps args = {
         .cap_set_id = VIRGL_RENDERER_CAPSET_DRM,
         .cap_set_ver = 0,
         .addr = VOID2U64(caps),
         .size = sizeof(*caps),
   };

   memset(caps, 0, sizeof(*caps));

   return virtio_ioctl(fd, VIRTGPU_GET_CAPS, &args);
}

static int
set_context(int fd)
{
   struct drm_virtgpu_context_set_param params[] = {
         { VIRTGPU_CONTEXT_PARAM_CAPSET_ID, VIRGL_RENDERER_CAPSET_DRM },
         { VIRTGPU_CONTEXT_PARAM_NUM_RINGS, 64 },
   };
   struct drm_virtgpu_context_init args = {
      .num_params = ARRAY_SIZE(params),
      .ctx_set_params = VOID2U64(params),
   };

   return virtio_ioctl(fd, VIRTGPU_CONTEXT_INIT, &args);
}

static void
set_debuginfo(struct fd_device *dev)
{
   const char *comm = util_get_process_name();
   static char cmdline[0x1000+1];
   int fd = open("/proc/self/cmdline", O_RDONLY);
   if (fd < 0)
      return;

   int n = read(fd, cmdline, sizeof(cmdline) - 1);
   if (n < 0)
      return;

   /* arguments are separated by NULL, convert to spaces: */
   for (int i = 0; i < n; i++) {
      if (cmdline[i] == '\0') {
         cmdline[i] = ' ';
      }
   }

   cmdline[n] = '\0';

   unsigned comm_len = strlen(comm) + 1;
   unsigned cmdline_len = strlen(cmdline) + 1;

   struct msm_ccmd_set_debuginfo_req *req;

   unsigned req_len = align(sizeof(*req) + comm_len + cmdline_len, 4);

   req = malloc(req_len);

   req->hdr         = MSM_CCMD(SET_DEBUGINFO, req_len);
   req->comm_len    = comm_len;
   req->cmdline_len = cmdline_len;

   memcpy(&req->payload[0], comm, comm_len);
   memcpy(&req->payload[comm_len], cmdline, cmdline_len);

   virtio_execbuf(dev, &req->hdr, false);

   free(req);
}

struct fd_device *
virtio_device_new(int fd, drmVersionPtr version)
{
   struct virgl_renderer_capset_drm caps;
   struct virtio_device *virtio_dev;
   struct fd_device *dev;
   int ret;

   STATIC_ASSERT(FD_BO_PREP_READ == MSM_PREP_READ);
   STATIC_ASSERT(FD_BO_PREP_WRITE == MSM_PREP_WRITE);
   STATIC_ASSERT(FD_BO_PREP_NOSYNC == MSM_PREP_NOSYNC);

   /* Debug option to force fallback to virgl: */
   if (debug_get_bool_option("FD_NO_VIRTIO", false))
      return NULL;

   ret = get_capset(fd, &caps);
   if (ret) {
      INFO_MSG("could not get caps: %s", strerror(errno));
      return NULL;
   }

   if (caps.context_type != VIRTGPU_DRM_CONTEXT_MSM) {
      INFO_MSG("wrong context_type: %u", caps.context_type);
      return NULL;
   }

   INFO_MSG("wire_format_version: %u", caps.wire_format_version);
   INFO_MSG("version_major:       %u", caps.version_major);
   INFO_MSG("version_minor:       %u", caps.version_minor);
   INFO_MSG("version_patchlevel:  %u", caps.version_patchlevel);
   INFO_MSG("has_cached_coherent: %u", caps.u.msm.has_cached_coherent);
   INFO_MSG("va_start:            0x%0"PRIx64, caps.u.msm.va_start);
   INFO_MSG("va_size:             0x%0"PRIx64, caps.u.msm.va_size);
   INFO_MSG("gpu_id:              %u", caps.u.msm.gpu_id);
   INFO_MSG("gmem_size:           %u", caps.u.msm.gmem_size);
   INFO_MSG("gmem_base:           0x%0" PRIx64, caps.u.msm.gmem_base);
   INFO_MSG("chip_id:             0x%0" PRIx64, caps.u.msm.chip_id);
   INFO_MSG("max_freq:            %u", caps.u.msm.max_freq);

   if (caps.wire_format_version != 2) {
      ERROR_MSG("Unsupported protocol version: %u", caps.wire_format_version);
      return NULL;
   }

   if ((caps.version_major != 1) || (caps.version_minor < FD_VERSION_SOFTPIN)) {
      ERROR_MSG("unsupported version: %u.%u.%u", caps.version_major,
                caps.version_minor, caps.version_patchlevel);
      return NULL;
   }

   if (!caps.u.msm.va_size) {
      ERROR_MSG("No address space");
      return NULL;
   }

   ret = set_context(fd);
   if (ret) {
      INFO_MSG("Could not set context type: %s", strerror(errno));
      return NULL;
   }

   virtio_dev = calloc(1, sizeof(*virtio_dev));
   if (!virtio_dev)
      return NULL;

   dev = &virtio_dev->base;
   dev->funcs = &funcs;
   dev->fd = fd;
   dev->version = caps.version_minor;
   dev->has_cached_coherent = caps.u.msm.has_cached_coherent;

   p_atomic_set(&virtio_dev->next_blob_id, 1);

   virtio_dev->caps = caps;

   util_queue_init(&dev->submit_queue, "sq", 8, 1, 0, NULL);

   dev->bo_size = sizeof(struct virtio_bo);

   simple_mtx_init(&virtio_dev->rsp_lock, mtx_plain);
   simple_mtx_init(&virtio_dev->eb_lock, mtx_plain);

   set_debuginfo(dev);

   util_vma_heap_init(&virtio_dev->address_space,
                      caps.u.msm.va_start,
                      caps.u.msm.va_size);
   simple_mtx_init(&virtio_dev->address_space_lock, mtx_plain);

   return dev;
}

void *
virtio_alloc_rsp(struct fd_device *dev, struct msm_ccmd_req *req, uint32_t sz)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);
   unsigned off;

   simple_mtx_lock(&virtio_dev->rsp_lock);

   sz = align(sz, 8);

   if ((virtio_dev->next_rsp_off + sz) >= virtio_dev->rsp_mem_len)
      virtio_dev->next_rsp_off = 0;

   off = virtio_dev->next_rsp_off;
   virtio_dev->next_rsp_off += sz;

   simple_mtx_unlock(&virtio_dev->rsp_lock);

   req->rsp_off = off;

   struct msm_ccmd_rsp *rsp = (void *)&virtio_dev->rsp_mem[off];
   rsp->len = sz;

   return rsp;
}

static int execbuf_flush_locked(struct fd_device *dev, int *out_fence_fd);

static int
execbuf_locked(struct fd_device *dev, void *cmd, uint32_t cmd_size,
               uint32_t *handles, uint32_t num_handles,
               int in_fence_fd, int *out_fence_fd, int ring_idx)
{
#define COND(bool, val) ((bool) ? (val) : 0)
   struct drm_virtgpu_execbuffer eb = {
         .flags = COND(out_fence_fd, VIRTGPU_EXECBUF_FENCE_FD_OUT) |
                  COND(in_fence_fd != -1, VIRTGPU_EXECBUF_FENCE_FD_IN) |
                  VIRTGPU_EXECBUF_RING_IDX,
         .fence_fd = in_fence_fd,
         .size  = cmd_size,
         .command = VOID2U64(cmd),
         .ring_idx = ring_idx,
         .bo_handles = VOID2U64(handles),
         .num_bo_handles = num_handles,
   };

   int ret = virtio_ioctl(dev->fd, VIRTGPU_EXECBUFFER, &eb);
   if (ret) {
      ERROR_MSG("EXECBUFFER failed: %s", strerror(errno));
      return ret;
   }

   if (out_fence_fd)
      *out_fence_fd = eb.fence_fd;

   return 0;
}

/**
 * Helper for "execbuf" ioctl.. note that in virtgpu execbuf is just
 * a generic "send commands to host", not necessarily specific to
 * cmdstream execution.
 *
 * Note that ring_idx 0 is the "CPU ring", ie. for synchronizing btwn
 * guest and host CPU.
 */
int
virtio_execbuf_fenced(struct fd_device *dev, struct msm_ccmd_req *req,
                      uint32_t *handles, uint32_t num_handles,
                      int in_fence_fd, int *out_fence_fd, int ring_idx)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);
   int ret;

   simple_mtx_lock(&virtio_dev->eb_lock);
   execbuf_flush_locked(dev, NULL);
   req->seqno = ++virtio_dev->next_seqno;

   ret = execbuf_locked(dev, req, req->len, handles, num_handles,
                        in_fence_fd, out_fence_fd, ring_idx);

   simple_mtx_unlock(&virtio_dev->eb_lock);

   return ret;
}

static int
execbuf_flush_locked(struct fd_device *dev, int *out_fence_fd)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);
   int ret;

   if (!virtio_dev->reqbuf_len)
      return 0;

   ret = execbuf_locked(dev, virtio_dev->reqbuf, virtio_dev->reqbuf_len,
                        NULL, 0, -1, out_fence_fd, 0);
   if (ret)
      return ret;

   virtio_dev->reqbuf_len = 0;
   virtio_dev->reqbuf_cnt = 0;

   return 0;
}

int
virtio_execbuf_flush(struct fd_device *dev)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);
   simple_mtx_lock(&virtio_dev->eb_lock);
   int ret = execbuf_flush_locked(dev, NULL);
   simple_mtx_unlock(&virtio_dev->eb_lock);
   return ret;
}

int
virtio_execbuf(struct fd_device *dev, struct msm_ccmd_req *req, bool sync)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);
   int fence_fd, ret = 0;

   simple_mtx_lock(&virtio_dev->eb_lock);
   req->seqno = ++virtio_dev->next_seqno;

   if ((virtio_dev->reqbuf_len + req->len) > sizeof(virtio_dev->reqbuf)) {
      ret = execbuf_flush_locked(dev, NULL);
      if (ret)
         goto out_unlock;
   }

   memcpy(&virtio_dev->reqbuf[virtio_dev->reqbuf_len], req, req->len);
   virtio_dev->reqbuf_len += req->len;
   virtio_dev->reqbuf_cnt++;

   if (!sync)
      goto out_unlock;

   ret = execbuf_flush_locked(dev, &fence_fd);

out_unlock:
   simple_mtx_unlock(&virtio_dev->eb_lock);

   if (ret)
      return ret;

   if (sync) {
      MESA_TRACE_BEGIN("virtio_execbuf sync");
      sync_wait(fence_fd, -1);
      close(fence_fd);
      virtio_host_sync(dev, req);
      MESA_TRACE_END();
   }

   return 0;
}

/**
 * Wait until host as processed the specified request.
 */
void
virtio_host_sync(struct fd_device *dev, const struct msm_ccmd_req *req)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);

   while (fd_fence_before(virtio_dev->shmem->seqno, req->seqno))
      sched_yield();
}

/**
 * Helper for simple pass-thru ioctls
 */
int
virtio_simple_ioctl(struct fd_device *dev, unsigned cmd, void *_req)
{
   unsigned req_len = sizeof(struct msm_ccmd_ioctl_simple_req);
   unsigned rsp_len = sizeof(struct msm_ccmd_ioctl_simple_rsp);

   req_len += _IOC_SIZE(cmd);
   if (cmd & IOC_OUT)
      rsp_len += _IOC_SIZE(cmd);

   uint8_t buf[req_len];
   struct msm_ccmd_ioctl_simple_req *req = (void *)buf;
   struct msm_ccmd_ioctl_simple_rsp *rsp;

   req->hdr = MSM_CCMD(IOCTL_SIMPLE, req_len);
   req->cmd = cmd;
   memcpy(req->payload, _req, _IOC_SIZE(cmd));

   rsp = virtio_alloc_rsp(dev, &req->hdr, rsp_len);

   int ret = virtio_execbuf(dev, &req->hdr, true);

   if (cmd & IOC_OUT)
      memcpy(_req, rsp->payload, _IOC_SIZE(cmd));

   ret = rsp->ret;

   return ret;
}
