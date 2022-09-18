/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Based on radv_radeon_winsys.h which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PVR_WINSYS_H
#define PVR_WINSYS_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "hwdef/rogue_hw_defs.h"
#include "pvr_limits.h"
#include "pvr_rogue_fw.h"
#include "pvr_types.h"
#include "util/macros.h"
#include "util/vma.h"
#include "vk_sync.h"

struct pvr_device_info;
struct pvr_device_runtime_info;

struct pvr_winsys_heaps {
   struct pvr_winsys_heap *general_heap;
   struct pvr_winsys_heap *pds_heap;
   struct pvr_winsys_heap *rgn_hdr_heap;
   struct pvr_winsys_heap *transfer_3d_heap;
   struct pvr_winsys_heap *usc_heap;
   struct pvr_winsys_heap *vis_test_heap;
};

struct pvr_winsys_static_data_offsets {
   uint64_t eot;
   uint64_t fence;
   uint64_t vdm_sync;
   uint64_t yuv_csc;
};

struct pvr_winsys_heap {
   struct pvr_winsys *ws;

   pvr_dev_addr_t base_addr;
   pvr_dev_addr_t reserved_addr;

   uint64_t size;
   uint64_t reserved_size;

   uint32_t page_size;
   uint32_t log2_page_size;

   struct util_vma_heap vma_heap;
   int ref_count;
   pthread_mutex_t lock;

   /* These are the offsets from the base at which static data might be
    * uploaded. Some of these might be invalid since the kernel might not
    * return all of these offsets per each heap as they might not be
    * applicable.
    * You should know which to use beforehand. There should be no need to check
    * whether an offset is valid or invalid.
    */
   struct pvr_winsys_static_data_offsets static_data_offsets;
};

enum pvr_winsys_bo_type {
   PVR_WINSYS_BO_TYPE_GPU = 0,
   PVR_WINSYS_BO_TYPE_DISPLAY = 1,
};

/**
 * \brief Flag passed to #pvr_winsys_ops.buffer_create to indicate that the
 * buffer should be CPU accessible. This is required in order to map the buffer
 * using #pvr_winsys_ops.buffer_map.
 */
#define PVR_WINSYS_BO_FLAG_CPU_ACCESS BITFIELD_BIT(0U)
/**
 * \brief Flag passed to #pvr_winsys_ops.buffer_create to indicate that, when
 * the buffer is mapped to the GPU using #pvr_winsys.vma_map, it should be
 * mapped uncached.
 */
#define PVR_WINSYS_BO_FLAG_GPU_UNCACHED BITFIELD_BIT(1U)
/**
 * \brief Flag passed to #pvr_winsys_ops.buffer_create to indicate that, when
 * the buffer is mapped to the GPU using #pvr_winsys.vma_map, it should only be
 * accessible to the Parameter Manager unit and firmware processor.
 */
#define PVR_WINSYS_BO_FLAG_PM_FW_PROTECT BITFIELD_BIT(2U)
/**
 * \brief Flag passed to #pvr_winsys_ops.buffer_create to indicate that the
 * buffer should be zeroed at allocation time.
 */
#define PVR_WINSYS_BO_FLAG_ZERO_ON_ALLOC BITFIELD_BIT(3U)

struct pvr_winsys_bo {
   struct pvr_winsys *ws;
   void *map;
   uint64_t size;

   bool is_imported;
};

struct pvr_winsys_vma {
   struct pvr_winsys_heap *heap;

   /* Buffer and offset this vma is bound to. */
   struct pvr_winsys_bo *bo;
   VkDeviceSize bo_offset;

   pvr_dev_addr_t dev_addr;
   uint64_t size;
   uint64_t mapped_size;
};

struct pvr_winsys_free_list {
   struct pvr_winsys *ws;
};

struct pvr_winsys_rt_dataset_create_info {
   /* Local freelist */
   struct pvr_winsys_free_list *local_free_list;

   /* ISP register values */
   uint32_t isp_merge_lower_x;
   uint32_t isp_merge_lower_y;
   uint32_t isp_merge_scale_x;
   uint32_t isp_merge_scale_y;
   uint32_t isp_merge_upper_x;
   uint32_t isp_merge_upper_y;
   uint32_t isp_mtile_size;

   /* PPP register values */
   uint64_t ppp_multi_sample_ctl;
   uint64_t ppp_multi_sample_ctl_y_flipped;
   uint32_t ppp_screen;

   /* TE register values */
   uint32_t te_aa;
   uint32_t te_mtile1;
   uint32_t te_mtile2;
   uint32_t te_screen;

   /* Allocations and associated information */
   pvr_dev_addr_t vheap_table_dev_addr;
   pvr_dev_addr_t rtc_dev_addr;

   pvr_dev_addr_t tpc_dev_addr;
   uint32_t tpc_stride;
   uint32_t tpc_size;

   struct {
      pvr_dev_addr_t pm_mlist_dev_addr;
      pvr_dev_addr_t macrotile_array_dev_addr;
      pvr_dev_addr_t rgn_header_dev_addr;
   } rt_datas[ROGUE_NUM_RTDATAS];
   uint64_t rgn_header_size;

   /* Miscellaneous */
   uint32_t mtile_stride;
   uint16_t max_rts;
};

struct pvr_winsys_rt_dataset {
   struct pvr_winsys *ws;
};

enum pvr_winsys_ctx_priority {
   PVR_WINSYS_CTX_PRIORITY_LOW,
   PVR_WINSYS_CTX_PRIORITY_MEDIUM,
   PVR_WINSYS_CTX_PRIORITY_HIGH,
};

struct pvr_winsys_render_ctx_create_info {
   enum pvr_winsys_ctx_priority priority;
   pvr_dev_addr_t vdm_callstack_addr;

   struct pvr_winsys_render_ctx_static_state {
      uint64_t vdm_ctx_state_base_addr;
      uint64_t geom_ctx_state_base_addr;

      struct {
         uint64_t vdm_ctx_store_task0;
         uint32_t vdm_ctx_store_task1;
         uint64_t vdm_ctx_store_task2;

         uint64_t vdm_ctx_resume_task0;
         uint32_t vdm_ctx_resume_task1;
         uint64_t vdm_ctx_resume_task2;
      } geom_state[2];
   } static_state;
};

struct pvr_winsys_render_ctx {
   struct pvr_winsys *ws;
};

struct pvr_winsys_compute_ctx_create_info {
   enum pvr_winsys_ctx_priority priority;

   struct pvr_winsys_compute_ctx_static_state {
      uint64_t cdm_ctx_store_pds0;
      uint64_t cdm_ctx_store_pds0_b;
      uint32_t cdm_ctx_store_pds1;

      uint64_t cdm_ctx_terminate_pds;
      uint32_t cdm_ctx_terminate_pds1;

      uint64_t cdm_ctx_resume_pds0;
      uint64_t cdm_ctx_resume_pds0_b;
   } static_state;
};

struct pvr_winsys_compute_ctx {
   struct pvr_winsys *ws;
};

struct pvr_winsys_transfer_ctx_create_info {
   enum pvr_winsys_ctx_priority priority;
};

struct pvr_winsys_transfer_ctx {
   struct pvr_winsys *ws;
};

#define PVR_WINSYS_TRANSFER_FLAG_START BITFIELD_BIT(0U)
#define PVR_WINSYS_TRANSFER_FLAG_END BITFIELD_BIT(1U)

#define PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT 16U
#define PVR_TRANSFER_MAX_RENDER_TARGETS 3U

struct pvr_winsys_transfer_regs {
   uint32_t event_pixel_pds_code;
   uint32_t event_pixel_pds_data;
   uint32_t event_pixel_pds_info;
   uint32_t isp_aa;
   uint32_t isp_bgobjvals;
   uint32_t isp_ctl;
   uint64_t isp_mtile_base;
   uint32_t isp_mtile_size;
   uint32_t isp_render;
   uint32_t isp_render_origin;
   uint32_t isp_rgn;
   uint64_t pbe_wordx_mrty[PVR_TRANSFER_MAX_RENDER_TARGETS *
                           ROGUE_NUM_PBESTATE_REG_WORDS];
   uint64_t pds_bgnd0_base;
   uint64_t pds_bgnd1_base;
   uint64_t pds_bgnd3_sizeinfo;
   uint32_t usc_clear_register0;
   uint32_t usc_clear_register1;
   uint32_t usc_clear_register2;
   uint32_t usc_clear_register3;
   uint32_t usc_pixel_output_ctrl;
};

struct pvr_winsys_transfer_submit_info {
   uint32_t frame_num;
   uint32_t job_num;

   /* waits and stage_flags are arrays of length wait_count. */
   struct vk_sync **waits;
   uint32_t wait_count;
   uint32_t *stage_flags;

   uint32_t cmd_count;
   struct {
      struct pvr_winsys_transfer_regs regs;

      /* Must be 0 or a combination of PVR_WINSYS_TRANSFER_FLAG_* flags. */
      uint32_t flags;
   } cmds[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT];
};

#define PVR_WINSYS_COMPUTE_FLAG_PREVENT_ALL_OVERLAP BITFIELD_BIT(0U)
#define PVR_WINSYS_COMPUTE_FLAG_SINGLE_CORE BITFIELD_BIT(1U)

struct pvr_winsys_compute_submit_info {
   uint32_t frame_num;
   uint32_t job_num;

   /* waits and stage_flags are arrays of length wait_count. */
   struct vk_sync **waits;
   uint32_t wait_count;
   uint32_t *stage_flags;

   struct {
      uint64_t tpu_border_colour_table;
      uint64_t cdm_ctrl_stream_base;
      uint64_t cdm_ctx_state_base_addr;
      uint32_t tpu;
      uint32_t cdm_resume_pds1;
      uint32_t cdm_item;
      uint32_t compute_cluster;
   } regs;

   /* Must be 0 or a combination of PVR_WINSYS_COMPUTE_FLAG_* flags. */
   uint32_t flags;
};

#define PVR_WINSYS_JOB_BO_FLAG_WRITE BITFIELD_BIT(0U)

struct pvr_winsys_job_bo {
   struct pvr_winsys_bo *bo;
   /* Must be 0 or a combination of PVR_WINSYS_JOB_BO_FLAG_* flags. */
   uint32_t flags;
};

#define PVR_WINSYS_GEOM_FLAG_FIRST_GEOMETRY BITFIELD_BIT(0U)
#define PVR_WINSYS_GEOM_FLAG_LAST_GEOMETRY BITFIELD_BIT(1U)
#define PVR_WINSYS_GEOM_FLAG_SINGLE_CORE BITFIELD_BIT(2U)

#define PVR_WINSYS_FRAG_FLAG_DEPTH_BUFFER_PRESENT BITFIELD_BIT(0U)
#define PVR_WINSYS_FRAG_FLAG_STENCIL_BUFFER_PRESENT BITFIELD_BIT(1U)
#define PVR_WINSYS_FRAG_FLAG_PREVENT_CDM_OVERLAP BITFIELD_BIT(2U)
#define PVR_WINSYS_FRAG_FLAG_SINGLE_CORE BITFIELD_BIT(3U)

struct pvr_winsys_render_submit_info {
   struct pvr_winsys_rt_dataset *rt_dataset;
   uint8_t rt_data_idx;

   uint32_t frame_num;
   uint32_t job_num;

   uint32_t bo_count;
   const struct pvr_winsys_job_bo *bos;

   /* FIXME: should this be flags instead? */
   bool run_frag;

   /* waits and stage_flags are arrays of length wait_count. */
   struct vk_sync **waits;
   uint32_t wait_count;
   uint32_t *stage_flags;

   struct pvr_winsys_geometry_state {
      struct {
         uint64_t pds_ctrl;
         uint32_t ppp_ctrl;
         uint32_t te_psg;
         uint32_t tpu;
         uint64_t tpu_border_colour_table;
         uint64_t vdm_ctrl_stream_base;
         uint32_t vdm_ctx_resume_task0_size;
      } regs;

      /* Must be 0 or a combination of PVR_WINSYS_GEOM_FLAG_* flags. */
      uint32_t flags;
   } geometry;

   struct pvr_winsys_fragment_state {
      struct {
         uint32_t event_pixel_pds_data;
         uint32_t event_pixel_pds_info;
         uint32_t isp_aa;
         uint32_t isp_bgobjdepth;
         uint32_t isp_bgobjvals;
         uint32_t isp_ctl;
         uint64_t isp_dbias_base;
         uint64_t isp_oclqry_base;
         uint64_t isp_scissor_base;
         uint64_t isp_stencil_load_store_base;
         uint64_t isp_zload_store_base;
         uint64_t isp_zlsctl;
         uint32_t isp_zls_pixels;
         uint64_t pbe_word[PVR_MAX_COLOR_ATTACHMENTS]
                          [ROGUE_NUM_PBESTATE_REG_WORDS];
         uint32_t pixel_phantom;
         uint64_t pds_bgnd[ROGUE_NUM_CR_PDS_BGRND_WORDS];
         uint64_t pds_pr_bgnd[ROGUE_NUM_CR_PDS_BGRND_WORDS];
         uint32_t tpu;
         uint64_t tpu_border_colour_table;
         uint32_t usc_pixel_output_ctrl;
      } regs;

      /* Must be 0 or a combination of PVR_WINSYS_FRAG_FLAG_* flags. */
      uint32_t flags;
      uint32_t zls_stride;
      uint32_t sls_stride;
   } fragment;
};

struct pvr_winsys_ops {
   void (*destroy)(struct pvr_winsys *ws);
   int (*device_info_init)(struct pvr_winsys *ws,
                           struct pvr_device_info *dev_info,
                           struct pvr_device_runtime_info *runtime_info);
   void (*get_heaps_info)(struct pvr_winsys *ws,
                          struct pvr_winsys_heaps *heaps);

   VkResult (*buffer_create)(struct pvr_winsys *ws,
                             uint64_t size,
                             uint64_t alignment,
                             enum pvr_winsys_bo_type type,
                             uint32_t flags,
                             struct pvr_winsys_bo **const bo_out);
   VkResult (*buffer_create_from_fd)(struct pvr_winsys *ws,
                                     int fd,
                                     struct pvr_winsys_bo **const bo_out);
   void (*buffer_destroy)(struct pvr_winsys_bo *bo);

   VkResult (*buffer_get_fd)(struct pvr_winsys_bo *bo, int *const fd_out);

   void *(*buffer_map)(struct pvr_winsys_bo *bo);
   void (*buffer_unmap)(struct pvr_winsys_bo *bo);

   struct pvr_winsys_vma *(*heap_alloc)(struct pvr_winsys_heap *heap,
                                        uint64_t size,
                                        uint64_t alignment);
   void (*heap_free)(struct pvr_winsys_vma *vma);

   pvr_dev_addr_t (*vma_map)(struct pvr_winsys_vma *vma,
                             struct pvr_winsys_bo *bo,
                             uint64_t offset,
                             uint64_t size);
   void (*vma_unmap)(struct pvr_winsys_vma *vma);

   VkResult (*free_list_create)(
      struct pvr_winsys *ws,
      struct pvr_winsys_vma *free_list_vma,
      uint32_t initial_num_pages,
      uint32_t max_num_pages,
      uint32_t grow_num_pages,
      uint32_t grow_threshold,
      struct pvr_winsys_free_list *parent_free_list,
      struct pvr_winsys_free_list **const free_list_out);
   void (*free_list_destroy)(struct pvr_winsys_free_list *free_list);

   VkResult (*render_target_dataset_create)(
      struct pvr_winsys *ws,
      const struct pvr_winsys_rt_dataset_create_info *create_info,
      struct pvr_winsys_rt_dataset **const rt_dataset_out);
   void (*render_target_dataset_destroy)(
      struct pvr_winsys_rt_dataset *rt_dataset);

   VkResult (*render_ctx_create)(
      struct pvr_winsys *ws,
      struct pvr_winsys_render_ctx_create_info *create_info,
      struct pvr_winsys_render_ctx **const ctx_out);
   void (*render_ctx_destroy)(struct pvr_winsys_render_ctx *ctx);
   VkResult (*render_submit)(
      const struct pvr_winsys_render_ctx *ctx,
      const struct pvr_winsys_render_submit_info *submit_info,
      struct vk_sync *signal_sync_geom,
      struct vk_sync *signal_sync_frag);

   VkResult (*compute_ctx_create)(
      struct pvr_winsys *ws,
      const struct pvr_winsys_compute_ctx_create_info *create_info,
      struct pvr_winsys_compute_ctx **const ctx_out);
   void (*compute_ctx_destroy)(struct pvr_winsys_compute_ctx *ctx);
   VkResult (*compute_submit)(
      const struct pvr_winsys_compute_ctx *ctx,
      const struct pvr_winsys_compute_submit_info *submit_info,
      struct vk_sync *signal_sync);

   VkResult (*transfer_ctx_create)(
      struct pvr_winsys *ws,
      const struct pvr_winsys_transfer_ctx_create_info *create_info,
      struct pvr_winsys_transfer_ctx **const ctx_out);
   void (*transfer_ctx_destroy)(struct pvr_winsys_transfer_ctx *ctx);
   VkResult (*transfer_submit)(
      const struct pvr_winsys_transfer_ctx *ctx,
      const struct pvr_winsys_transfer_submit_info *submit_info,
      struct vk_sync *signal_sync);

   VkResult (*null_job_submit)(struct pvr_winsys *ws,
                               struct vk_sync **waits,
                               uint32_t wait_count,
                               struct vk_sync *signal_sync);
};

struct pvr_winsys {
   uint64_t page_size;
   uint32_t log2_page_size;

   const struct vk_sync_type *sync_types[2];
   struct vk_sync_type syncobj_type;

   const struct pvr_winsys_ops *ops;
};

void pvr_winsys_destroy(struct pvr_winsys *ws);
struct pvr_winsys *pvr_winsys_create(int master_fd,
                                     int render_fd,
                                     const VkAllocationCallbacks *alloc);

#endif /* PVR_WINSYS_H */
