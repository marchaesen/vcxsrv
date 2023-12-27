/*
 * Copyright © 2009 Corbin Simpson
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AMDGPU_WINSYS_H
#define AMDGPU_WINSYS_H

#include "pipebuffer/pb_cache.h"
#include "pipebuffer/pb_slab.h"
#include "winsys/radeon_winsys.h"
#include "util/simple_mtx.h"
#include "util/u_queue.h"
#include <amdgpu.h>

struct amdgpu_cs;

#define NUM_SLAB_ALLOCATORS 3

/* DRM file descriptors, file descriptions and buffer sharing.
 *
 * amdgpu_device_initialize first argument is a file descriptor (fd)
 * representing a specific GPU.
 * If a fd is duplicated using os_dupfd_cloexec,
 * the file description will remain the same (os_same_file_description will
 * return 0).
 * But if the same device is re-opened, the fd and the file description will
 * be different.
 *
 * amdgpu_screen_winsys's fd tracks the file description which was
 * given to amdgpu_winsys_create. This is the fd used by the application
 * using the driver and may be used in other ioctl (eg: drmModeAddFB)
 *
 * amdgpu_winsys's fd is the file description used to initialize the
 * device handle in libdrm_amdgpu.
 *
 * The 2 fds can be different, even in systems with a single GPU, eg: if
 * radv is initialized before radeonsi.
 *
 * This fd tracking is useful for buffer sharing because KMS/GEM handles are
 * specific to a DRM file description, i.e. the same handle value may refer
 * to different underlying BOs in different DRM file descriptions.
 * As an example, if an app wants to use drmModeAddFB it'll need a KMS handle
 * valid for its fd (== amdgpu_screen_winsys::fd).
 * If both fds are identical, there's nothing to do: bo->u.real.kms_handle
 * can be used directly (see amdgpu_bo_get_handle).
 * If they're different, the BO has to be exported from the device fd as
 * a dma-buf, then imported from the app fd as a KMS handle.
 */

struct amdgpu_screen_winsys {
   struct radeon_winsys base;
   struct amdgpu_winsys *aws;
   /* See comment above */
   int fd;
   struct pipe_reference reference;
   struct amdgpu_screen_winsys *next;

   /* Maps a BO to its KMS handle valid for this DRM file descriptor
    * Protected by amdgpu_winsys::sws_list_lock
    */
   struct hash_table *kms_handles;
};

struct amdgpu_winsys {
   struct pipe_reference reference;
   /* See comment above */
   int fd;

   struct pb_cache bo_cache;

   /* Each slab buffer can only contain suballocations of equal sizes, so we
    * need to layer the allocators, so that we don't waste too much memory.
    */
   struct pb_slabs bo_slabs[NUM_SLAB_ALLOCATORS];

   amdgpu_device_handle dev;

   simple_mtx_t bo_fence_lock;

   int num_cs; /* The number of command streams created. */
   uint32_t surf_index_color;
   uint32_t surf_index_fmask;
   uint32_t next_bo_unique_id;
   uint64_t allocated_vram;
   uint64_t allocated_gtt;
   uint64_t mapped_vram;
   uint64_t mapped_gtt;
   uint64_t slab_wasted_vram;
   uint64_t slab_wasted_gtt;
   uint64_t buffer_wait_time; /* time spent in buffer_wait in ns */
   uint64_t num_gfx_IBs;
   uint64_t num_sdma_IBs;
   uint64_t num_mapped_buffers;
   uint64_t gfx_bo_list_counter;
   uint64_t gfx_ib_size_counter;

   struct radeon_info info;

   /* multithreaded IB submission */
   struct util_queue cs_queue;

   struct ac_addrlib *addrlib;

   bool check_vm;
   bool noop_cs;
   bool reserve_vmid;
   bool zero_all_vram_allocs;
#if DEBUG
   bool debug_all_bos;

   /* List of all allocated buffers */
   simple_mtx_t global_bo_list_lock;
   struct list_head global_bo_list;
   unsigned num_buffers;
#endif

   /* Single-linked list of all structs amdgpu_screen_winsys referencing this
    * struct amdgpu_winsys
    */
   simple_mtx_t sws_list_lock;
   struct amdgpu_screen_winsys *sws_list;

   /* For returning the same amdgpu_winsys_bo instance for exported
    * and re-imported buffers. */
   struct hash_table *bo_export_table;
   simple_mtx_t bo_export_table_lock;

   /* Since most winsys functions require struct radeon_winsys *, dummy_ws.base is used
    * for invoking them because sws_list can be NULL.
    */
   struct amdgpu_screen_winsys dummy_ws;
};

static inline struct amdgpu_screen_winsys *
amdgpu_screen_winsys(struct radeon_winsys *base)
{
   return (struct amdgpu_screen_winsys*)base;
}

static inline struct amdgpu_winsys *
amdgpu_winsys(struct radeon_winsys *base)
{
   return amdgpu_screen_winsys(base)->aws;
}

void amdgpu_surface_init_functions(struct amdgpu_screen_winsys *ws);

#endif
