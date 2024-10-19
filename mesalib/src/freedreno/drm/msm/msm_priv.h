/*
 * Copyright © 2012-2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef MSM_PRIV_H_
#define MSM_PRIV_H_

#include "freedreno_drmif.h"
#include "freedreno_priv.h"
#include "freedreno_rd_output.h"

#include "util/timespec.h"
#include "util/u_process.h"

#ifndef __user
#define __user
#endif

#include "drm-uapi/msm_drm.h"

struct msm_device {
   struct fd_device base;
};
FD_DEFINE_CAST(fd_device, msm_device);

struct fd_device *msm_device_new(int fd, drmVersionPtr version);

struct msm_pipe {
   struct fd_pipe base;
   uint32_t pipe;
   uint32_t gpu_id;
   uint64_t chip_id;
   uint64_t gmem_base;
   uint32_t gmem;
   uint32_t queue_id;
};
FD_DEFINE_CAST(fd_pipe, msm_pipe);

struct fd_pipe *msm_pipe_new(struct fd_device *dev, enum fd_pipe_id id,
                             uint32_t prio);

struct fd_ringbuffer *msm_ringbuffer_new_object(struct fd_pipe *pipe,
                                                uint32_t size);

struct fd_submit *msm_submit_new(struct fd_pipe *pipe);
struct fd_submit *msm_submit_sp_new(struct fd_pipe *pipe);

struct msm_bo {
   struct fd_bo base;
   uint64_t offset;
};
FD_DEFINE_CAST(fd_bo, msm_bo);

struct fd_bo *msm_bo_new(struct fd_device *dev, uint32_t size, uint32_t flags);
struct fd_bo *msm_bo_from_handle(struct fd_device *dev, uint32_t size,
                                 uint32_t handle);

static inline void
msm_dump_submit(struct drm_msm_gem_submit *req)
{
   for (unsigned i = 0; i < req->nr_bos; i++) {
      struct drm_msm_gem_submit_bo *bos = U642VOID(req->bos);
      struct drm_msm_gem_submit_bo *bo = &bos[i];
      ERROR_MSG("  bos[%d]: handle=%u, flags=%x", i, bo->handle, bo->flags);
   }
   for (unsigned i = 0; i < req->nr_cmds; i++) {
      struct drm_msm_gem_submit_cmd *cmds = U642VOID(req->cmds);
      struct drm_msm_gem_submit_cmd *cmd = &cmds[i];
      struct drm_msm_gem_submit_reloc *relocs = U642VOID(cmd->relocs);
      ERROR_MSG("  cmd[%d]: type=%u, submit_idx=%u, submit_offset=%u, size=%u",
                i, cmd->type, cmd->submit_idx, cmd->submit_offset, cmd->size);
      for (unsigned j = 0; j < cmd->nr_relocs; j++) {
         struct drm_msm_gem_submit_reloc *r = &relocs[j];
         ERROR_MSG(
            "    reloc[%d]: submit_offset=%u, or=%08x, shift=%d, reloc_idx=%u"
            ", reloc_offset=%" PRIu64,
            j, r->submit_offset, r->or, r->shift, r->reloc_idx,
            (uint64_t)r->reloc_offset);
      }
   }
}

static inline bool
__should_dump(struct fd_bo *bo)
{
   return (bo->reloc_flags & FD_RELOC_DUMP) || FD_RD_DUMP(FULL);
}

static inline void
__snapshot_buf(struct fd_rd_output *rd, struct fd_bo *bo, uint64_t iova,
               uint32_t size, bool full)
{
   uint64_t offset = 0;

   if (iova) {
      offset = iova - fd_bo_get_iova(bo);
   } else {
      iova = fd_bo_get_iova(bo);
      size = bo->size;
   }

   fd_rd_output_write_section(rd, RD_GPUADDR, (uint32_t[]){
      iova, size, iova >> 32
   }, 12);

   if (!full)
      return;

   const char *buf = __fd_bo_map(bo);
   buf += offset;
   fd_rd_output_write_section(rd, RD_BUFFER_CONTENTS, buf, size);
}

static inline void
msm_dump_rd(struct fd_pipe *pipe, struct drm_msm_gem_submit *req)
{
   struct fd_rd_output *rd = &pipe->dev->rd;

   if (!fd_rd_dump_env.flags || !req->nr_cmds ||
       !fd_rd_output_begin(rd, req->fence))
      return;

   if (FD_RD_DUMP(FULL)) {
      fd_pipe_wait(pipe, &(struct fd_fence) {
         /* this is cheating a bit, but msm_pipe_wait only needs kfence */
         .kfence = req->fence,
      });
   }

   const char *procname = util_get_process_name();
   fd_rd_output_write_section(rd, RD_CHIP_ID, &to_msm_pipe(pipe)->chip_id, 8);
   fd_rd_output_write_section(rd, RD_CMD, procname, strlen(procname));

   struct drm_msm_gem_submit_bo *bos = U642VOID(req->bos);
   struct drm_msm_gem_submit_cmd *cmds = U642VOID(req->cmds);

   for (unsigned i = 0; i < req->nr_bos; i++) {
      /* This size param to fd_bo_from_handle() only matters if the bo isn't already in
       * the handle table.  Which it should be.
       */
      struct fd_bo *bo = fd_bo_from_handle(pipe->dev, bos[i].handle, 0);

      __snapshot_buf(rd, bo, 0, 0, __should_dump(bo));

      fd_bo_del(bo);
   }

   for (unsigned i = 0; i < req->nr_cmds; i++) {
      struct drm_msm_gem_submit_cmd *cmd = &cmds[i];
      struct fd_bo *bo = fd_bo_from_handle(pipe->dev, bos[cmd->submit_idx].handle, 0);
      uint64_t iova = fd_bo_get_iova(bo) + cmd->submit_offset;

      /* snapshot cmdstream bo's (if we haven't already): */
      if (!__should_dump(bo))
         __snapshot_buf(rd, bo, iova, cmd->size, true);

      fd_rd_output_write_section(rd, RD_CMDSTREAM_ADDR, (uint32_t[]){
         iova, cmd->size >> 2, iova >> 32
      }, 12);

      fd_bo_del(bo);
   }

   fd_rd_output_end(rd);
}

static inline void
get_abs_timeout(struct drm_msm_timespec *tv, uint64_t ns)
{
   struct timespec t;

   if (ns == OS_TIMEOUT_INFINITE)
      ns = 3600ULL * NSEC_PER_SEC; /* 1 hour timeout is almost infinite */

   clock_gettime(CLOCK_MONOTONIC, &t);
   tv->tv_sec = t.tv_sec + ns / NSEC_PER_SEC;
   tv->tv_nsec = t.tv_nsec + ns % NSEC_PER_SEC;
   if (tv->tv_nsec >= NSEC_PER_SEC) { /* handle nsec overflow */
      tv->tv_nsec -= NSEC_PER_SEC;
      tv->tv_sec++;
   }
}

#endif /* MSM_PRIV_H_ */
