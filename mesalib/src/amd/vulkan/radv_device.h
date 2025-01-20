/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DEVICE_H
#define RADV_DEVICE_H

#include "ac_descriptors.h"
#include "ac_spm.h"
#include "ac_sqtt.h"

#include "util/mesa-blake3.h"

#include "radv_pipeline.h"
#include "radv_printf.h"
#include "radv_queue.h"
#include "radv_radeon_winsys.h"
#include "radv_rra.h"
#include "radv_shader.h"

#include "vk_acceleration_structure.h"
#include "vk_device.h"
#include "vk_meta.h"
#include "vk_texcompress_astc.h"
#include "vk_texcompress_etc2.h"

#define RADV_NUM_HW_CTX (RADEON_CTX_PRIORITY_REALTIME + 1)

struct radv_image_view;

enum radv_dispatch_table {
   RADV_DEVICE_DISPATCH_TABLE,
   RADV_ANNOTATE_DISPATCH_TABLE,
   RADV_APP_DISPATCH_TABLE,
   RADV_RGP_DISPATCH_TABLE,
   RADV_RRA_DISPATCH_TABLE,
   RADV_RMV_DISPATCH_TABLE,
   RADV_CTX_ROLL_DISPATCH_TABLE,
   RADV_DISPATCH_TABLE_COUNT,
};

struct radv_layer_dispatch_tables {
   struct vk_device_dispatch_table annotate;
   struct vk_device_dispatch_table app;
   struct vk_device_dispatch_table rgp;
   struct vk_device_dispatch_table rra;
   struct vk_device_dispatch_table rmv;
   struct vk_device_dispatch_table ctx_roll;
};

struct radv_device_cache_key {
   uint32_t keep_shader_info : 1;
   uint32_t disable_trunc_coord : 1;
   uint32_t image_2d_view_of_3d : 1;
   uint32_t mesh_shader_queries : 1;
   uint32_t primitives_generated_query : 1;
   uint32_t trap_excp_flags : 4;
};

enum radv_force_vrs {
   RADV_FORCE_VRS_1x1 = 0,
   RADV_FORCE_VRS_2x2,
   RADV_FORCE_VRS_2x1,
   RADV_FORCE_VRS_1x2,
};

struct radv_notifier {
   int fd;
   int watch;
   bool quit;
   thrd_t thread;
};

struct radv_meta_state {
   VkAllocationCallbacks alloc;

   VkPipelineCache cache;
   uint32_t initial_cache_entries;

   /*
    * For on-demand pipeline creation, makes sure that
    * only one thread tries to build a pipeline at the same time.
    */
   mtx_t mtx;

   struct {
      VkPipelineLayout encode_p_layout;
      VkPipeline encode_pipeline;
      VkPipeline encode_compact_pipeline;
      VkPipelineLayout header_p_layout;
      VkPipeline header_pipeline;
      VkPipelineLayout update_p_layout;
      VkPipeline update_pipeline;
      VkPipelineLayout copy_p_layout;
      VkPipeline copy_pipeline;

      struct radix_sort_vk *radix_sort;
      struct vk_acceleration_structure_build_args build_args;

      struct {
         VkBuffer buffer;
         VkDeviceMemory memory;
         VkAccelerationStructureKHR accel_struct;
      } null;
   } accel_struct_build;

   struct vk_texcompress_etc2_state etc_decode;

   struct vk_texcompress_astc_state *astc_decode;

   struct vk_meta_device device;
};

struct radv_memory_trace_data {
   /* ID of the PTE update event in ftrace data */
   uint16_t ftrace_update_ptes_id;

   uint32_t num_cpus;
   int *pipe_fds;
};

struct radv_sqtt_timestamp {
   uint8_t *map;
   unsigned offset;
   uint64_t size;
   struct radeon_winsys_bo *bo;
   struct list_head list;
};

#define RADV_BORDER_COLOR_COUNT       4096
#define RADV_BORDER_COLOR_BUFFER_SIZE (sizeof(VkClearColorValue) * RADV_BORDER_COLOR_COUNT)

struct radv_device_border_color_data {
   bool used[RADV_BORDER_COLOR_COUNT];

   struct radeon_winsys_bo *bo;
   VkClearColorValue *colors_gpu_ptr;

   /* Mutex is required to guarantee vkCreateSampler thread safety
    * given that we are writing to a buffer and checking color occupation */
   mtx_t mutex;
};

struct radv_pso_cache_stats {
   uint32_t hits;
   uint32_t misses;
};

struct radv_device {
   struct vk_device vk;

   struct radeon_winsys *ws;

   struct radv_layer_dispatch_tables layer_dispatch;

   struct radeon_winsys_ctx *hw_ctx[RADV_NUM_HW_CTX];
   struct radv_meta_state meta_state;

   struct radv_queue *queues[RADV_MAX_QUEUE_FAMILIES];
   int queue_count[RADV_MAX_QUEUE_FAMILIES];

   bool pbb_allowed;
   uint32_t scratch_waves;
   uint32_t dispatch_initiator;
   uint32_t dispatch_initiator_task;

   /* MSAA sample locations.
    * The first index is the sample index.
    * The second index is the coordinate: X, Y. */
   float sample_locations_1x[1][2];
   float sample_locations_2x[2][2];
   float sample_locations_4x[4][2];
   float sample_locations_8x[8][2];

   /* GFX7 and later */
   uint32_t gfx_init_size_dw;
   struct radeon_winsys_bo *gfx_init;

   struct radeon_winsys_bo *trace_bo;
   struct radv_trace_data *trace_data;

   /* Whether to keep shader debug info, for debugging. */
   bool keep_shader_info;

   /* Backup in-memory cache to be used if the app doesn't provide one */
   struct vk_pipeline_cache *mem_cache;

   /*
    * use different counters so MSAA MRTs get consecutive surface indices,
    * even if MASK is allocated in between.
    */
   uint32_t image_mrt_offset_counter;
   uint32_t fmask_mrt_offset_counter;

   struct list_head shader_arenas;
   struct hash_table_u64 *capture_replay_arena_vas;
   unsigned shader_arena_shift;
   uint8_t shader_free_list_mask;
   struct radv_shader_free_list shader_free_list;
   struct radv_shader_free_list capture_replay_free_list;
   struct list_head shader_block_obj_pool;
   mtx_t shader_arena_mutex;

   mtx_t shader_upload_hw_ctx_mutex;
   struct radeon_winsys_ctx *shader_upload_hw_ctx;
   VkSemaphore shader_upload_sem;
   uint64_t shader_upload_seq;
   struct list_head shader_dma_submissions;
   mtx_t shader_dma_submission_list_mutex;
   cnd_t shader_dma_submission_list_cond;

   /* Whether to DMA shaders to invisible VRAM or to upload directly through BAR. */
   bool shader_use_invisible_vram;

   /* Whether to inline the compute dispatch size in user sgprs. */
   bool load_grid_size_from_user_sgpr;

   /* Whether the driver uses a global BO list. */
   bool use_global_bo_list;

   /* Whether anisotropy is forced with RADV_TEX_ANISO (-1 is disabled). */
   int force_aniso;

   /* Always disable TRUNC_COORD. */
   bool disable_trunc_coord;

   struct radv_device_border_color_data border_color_data;

   /* Thread trace. */
   struct ac_sqtt sqtt;
   bool sqtt_enabled;
   bool sqtt_triggered;

   /* SQTT timestamps for queue events. */
   simple_mtx_t sqtt_timestamp_mtx;
   struct radv_sqtt_timestamp sqtt_timestamp;

   /* SQTT timed cmd buffers. */
   simple_mtx_t sqtt_command_pool_mtx;
   struct vk_command_pool *sqtt_command_pool[2];

   /* Memory trace. */
   struct radv_memory_trace_data memory_trace;

   /* SPM. */
   struct ac_spm spm;

   /* Radeon Raytracing Analyzer trace. */
   struct radv_rra_trace_data rra_trace;

   FILE *ctx_roll_file;
   simple_mtx_t ctx_roll_mtx;

   /* Trap handler. */
   struct radv_shader *trap_handler_shader;
   struct radeon_winsys_bo *tma_bo; /* Trap Memory Address */
   uint32_t *tma_ptr;

   /* Overallocation. */
   bool overallocation_disallowed;
   uint64_t allocated_memory_size[VK_MAX_MEMORY_HEAPS];
   mtx_t overallocation_mutex;

   /* RADV_FORCE_VRS. */
   struct radv_notifier notifier;
   enum radv_force_vrs force_vrs;

   /* Depth image for VRS when not bound by the app. */
   struct {
      struct radv_image *image;
      struct radv_buffer *buffer; /* HTILE */
      struct radv_device_memory *mem;
   } vrs;

   /* Prime blit sdma queue */
   struct radv_queue *private_sdma_queue;

   struct radv_shader_part_cache vs_prologs;
   struct radv_shader_part *simple_vs_prologs[MAX_VERTEX_ATTRIBS];
   struct radv_shader_part *instance_rate_vs_prologs[816];

   struct radv_shader_part_cache ps_epilogs;

   simple_mtx_t trace_mtx;

   /* Whether per-vertex VRS is forced. */
   bool force_vrs_enabled;

   simple_mtx_t pstate_mtx;
   unsigned pstate_cnt;

   /* BO to contain some performance counter helpers:
    * - A lock for profiling cmdbuffers.
    * - a temporary fence for the end query synchronization.
    * - the pass to use for profiling. (as an array of bools)
    */
   struct radeon_winsys_bo *perf_counter_bo;

   /* Interleaved lock/unlock commandbuffers for perfcounter passes. */
   struct radeon_cmdbuf **perf_counter_lock_cs;

   bool uses_shadow_regs;

   struct hash_table *rt_handles;
   simple_mtx_t rt_handles_mtx;

   struct radv_printf_data printf;

   struct radv_device_cache_key cache_key;
   blake3_hash cache_hash;

   /* Not NULL if a GPU hang report has been generated for VK_EXT_device_fault. */
   char *gpu_hang_report;

   /* PSO cache stats */
   simple_mtx_t pso_cache_stats_mtx;
   struct radv_pso_cache_stats pso_cache_stats[RADV_PIPELINE_TYPE_COUNT];

   struct radv_address_binding_tracker *addr_binding_tracker;
};

VK_DEFINE_HANDLE_CASTS(radv_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)

static inline struct radv_physical_device *
radv_device_physical(const struct radv_device *dev)
{
   return (struct radv_physical_device *)dev->vk.physical;
}

static inline bool
radv_uses_primitives_generated_query(const struct radv_device *device)
{
   return device->vk.enabled_features.primitivesGeneratedQuery ||
          device->vk.enabled_features.primitivesGeneratedQueryWithRasterizerDiscard ||
          device->vk.enabled_features.primitivesGeneratedQueryWithNonZeroStreams;
}

static inline bool
radv_uses_image_float32_atomics(const struct radv_device *device)
{
   return device->vk.enabled_features.shaderImageFloat32Atomics ||
          device->vk.enabled_features.sparseImageFloat32Atomics ||
          device->vk.enabled_features.shaderImageFloat32AtomicMinMax ||
          device->vk.enabled_features.sparseImageFloat32AtomicMinMax;
}

VkResult radv_device_init_vrs_state(struct radv_device *device);

unsigned radv_get_default_max_sample_dist(int log_samples);

void radv_emit_default_sample_locations(const struct radv_physical_device *pdev, struct radeon_cmdbuf *cs,
                                        int nr_samples);

unsigned radv_get_dcc_max_uncompressed_block_size(const struct radv_device *device, const struct radv_image *image);

struct radv_color_buffer_info {
   struct ac_cb_surface ac;
};

struct radv_ds_buffer_info {
   struct ac_ds_surface ac;

   uint32_t db_render_override2;
   uint32_t db_render_control;
};

void radv_initialise_color_surface(struct radv_device *device, struct radv_color_buffer_info *cb,
                                   struct radv_image_view *iview);

void radv_initialise_vrs_surface(struct radv_image *image, struct radv_buffer *htile_buffer,
                                 struct radv_ds_buffer_info *ds);


void radv_initialise_ds_surface(const struct radv_device *device, struct radv_ds_buffer_info *ds,
                                struct radv_image_view *iview, VkImageAspectFlags ds_aspects);

void radv_gfx11_set_db_render_control(const struct radv_device *device, unsigned num_samples,
                                      unsigned *db_render_control);

bool radv_device_set_pstate(struct radv_device *device, bool enable);

bool radv_device_acquire_performance_counters(struct radv_device *device);

void radv_device_release_performance_counters(struct radv_device *device);

#endif /* RADV_DEVICE_H */
