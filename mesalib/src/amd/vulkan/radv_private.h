/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef RADV_PRIVATE_H
#define RADV_PRIVATE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_VALGRIND
#include <memcheck.h>
#include <valgrind.h>
#define VG(x) x
#else
#define VG(x) ((void)0)
#endif

#include "c11/threads.h"
#ifndef _WIN32
#include <amdgpu.h>
#include <xf86drm.h>
#endif
#include "compiler/shader_enums.h"
#include "util/bitscan.h"
#include "util/cnd_monotonic.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/rwlock.h"
#include "util/xmlconfig.h"
#include "vk_alloc.h"
#include "vk_debug_report.h"
#include "vk_device.h"
#include "vk_format.h"
#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_shader_module.h"
#include "vk_util.h"

#include "ac_binary.h"
#include "ac_gpu_info.h"
#include "ac_shader_util.h"
#include "ac_sqtt.h"
#include "ac_surface.h"
#include "radv_constants.h"
#include "radv_descriptor_set.h"
#include "radv_radeon_winsys.h"
#include "sid.h"

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include "radv_entrypoints.h"

#include "wsi_common.h"

/* Helper to determine if we should compile
 * any of the Android AHB support.
 *
 * To actually enable the ext we also need
 * the necessary kernel support.
 */
#if defined(ANDROID) && ANDROID_API_LEVEL >= 26
#define RADV_SUPPORT_ANDROID_HARDWARE_BUFFER 1
#include <vndk/hardware_buffer.h>
#else
#define RADV_SUPPORT_ANDROID_HARDWARE_BUFFER 0
#endif

#ifdef _WIN32
#define RADV_SUPPORT_CALIBRATED_TIMESTAMPS 0
#else
#define RADV_SUPPORT_CALIBRATED_TIMESTAMPS 1
#endif

#ifdef _WIN32
#define radv_printflike(a, b)
#else
#define radv_printflike(a, b) __attribute__((__format__(__printf__, a, b)))
#endif

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

static inline uint32_t
align_u32_npot(uint32_t v, uint32_t a)
{
   return (v + a - 1) / a * a;
}

static inline uint64_t
align_u64(uint64_t v, uint64_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

static inline int32_t
align_i32(int32_t v, int32_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

/** Alignment must be a power of 2. */
static inline bool
radv_is_aligned(uintmax_t n, uintmax_t a)
{
   assert(a == (a & -a));
   return (n & (a - 1)) == 0;
}

static inline uint32_t
round_up_u32(uint32_t v, uint32_t a)
{
   return (v + a - 1) / a;
}

static inline uint64_t
round_up_u64(uint64_t v, uint64_t a)
{
   return (v + a - 1) / a;
}

static inline uint32_t
radv_minify(uint32_t n, uint32_t levels)
{
   if (unlikely(n == 0))
      return 0;
   else
      return MAX2(n >> levels, 1);
}
static inline float
radv_clamp_f(float f, float min, float max)
{
   assert(min < max);

   if (f > max)
      return max;
   else if (f < min)
      return min;
   else
      return f;
}

static inline bool
radv_clear_mask(uint32_t *inout_mask, uint32_t clear_mask)
{
   if (*inout_mask & clear_mask) {
      *inout_mask &= ~clear_mask;
      return true;
   } else {
      return false;
   }
}

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

struct radv_image_view;
struct radv_instance;

VkResult __vk_errorv(struct radv_instance *instance, const void *object,
                     VkDebugReportObjectTypeEXT type, VkResult error, const char *file, int line,
                     const char *format, va_list args);

VkResult __vk_errorf(struct radv_instance *instance, const void *object,
                     VkDebugReportObjectTypeEXT type, VkResult error, const char *file, int line,
                     const char *format, ...) radv_printflike(7, 8);

#define vk_error(instance, error)                                                                  \
   __vk_errorf(instance, NULL, VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT, error, __FILE__, __LINE__, \
               NULL);
#define vk_errorf(instance, error, format, ...)                                                    \
   __vk_errorf(instance, NULL, VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT, error, __FILE__, __LINE__, \
               format, ##__VA_ARGS__);

void radv_loge(const char *format, ...) radv_printflike(1, 2);
void radv_loge_v(const char *format, va_list va);
void radv_logi(const char *format, ...) radv_printflike(1, 2);
void radv_logi_v(const char *format, va_list va);

/* A non-fatal assert.  Useful for debugging. */
#ifdef NDEBUG
#define radv_assert(x)                                                                             \
   do {                                                                                            \
   } while (0)
#else
#define radv_assert(x)                                                                             \
   do {                                                                                            \
      if (unlikely(!(x)))                                                                          \
         fprintf(stderr, "%s:%d ASSERT: %s\n", __FILE__, __LINE__, #x);                            \
   } while (0)
#endif

int radv_get_instance_entrypoint_index(const char *name);
int radv_get_device_entrypoint_index(const char *name);
int radv_get_physical_device_entrypoint_index(const char *name);

const char *radv_get_instance_entry_name(int index);
const char *radv_get_physical_device_entry_name(int index);
const char *radv_get_device_entry_name(int index);

struct radv_physical_device {
   struct vk_physical_device vk;

   /* Link in radv_instance::physical_devices */
   struct list_head link;

   struct radv_instance *instance;

   struct radeon_winsys *ws;
   struct radeon_info rad_info;
   char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t cache_uuid[VK_UUID_SIZE];

   int local_fd;
   int master_fd;
   struct wsi_device wsi_device;

   bool out_of_order_rast_allowed;

   /* Whether DCC should be enabled for MSAA textures. */
   bool dcc_msaa_allowed;

   /* Whether to enable NGG. */
   bool use_ngg;

   /* Whether to enable NGG streamout. */
   bool use_ngg_streamout;

   /* Number of threads per wave. */
   uint8_t ps_wave_size;
   uint8_t cs_wave_size;
   uint8_t ge_wave_size;

   /* Whether to use the LLVM compiler backend */
   bool use_llvm;

   /* This is the drivers on-disk cache used as a fallback as opposed to
    * the pipeline cache defined by apps.
    */
   struct disk_cache *disk_cache;

   VkPhysicalDeviceMemoryProperties memory_properties;
   enum radeon_bo_domain memory_domains[VK_MAX_MEMORY_TYPES];
   enum radeon_bo_flag memory_flags[VK_MAX_MEMORY_TYPES];
   unsigned heaps;

#ifndef _WIN32
   int available_nodes;
   drmPciBusInfo bus_info;

   dev_t primary_devid;
   dev_t render_devid;
#endif

   nir_shader_compiler_options nir_options;
};

struct radv_instance {
   struct vk_instance vk;

   VkAllocationCallbacks alloc;

   uint64_t debug_flags;
   uint64_t perftest_flags;

   bool physical_devices_enumerated;
   struct list_head physical_devices;

   struct driOptionCache dri_options;
   struct driOptionCache available_dri_options;

   /**
    * Workarounds for game bugs.
    */
   bool enable_mrt_output_nan_fixup;
   bool disable_tc_compat_htile_in_general;
   bool disable_shrink_image_store;
   bool absolute_depth_bias;
   bool report_apu_as_dgpu;
};

VkResult radv_init_wsi(struct radv_physical_device *physical_device);
void radv_finish_wsi(struct radv_physical_device *physical_device);

struct cache_entry;

struct radv_pipeline_cache {
   struct vk_object_base base;
   struct radv_device *device;
   mtx_t mutex;
   VkPipelineCacheCreateFlags flags;

   uint32_t total_size;
   uint32_t table_size;
   uint32_t kernel_count;
   struct cache_entry **hash_table;
   bool modified;

   VkAllocationCallbacks alloc;
};

struct radv_pipeline_key {
   uint32_t instance_rate_inputs;
   uint32_t instance_rate_divisors[MAX_VERTEX_ATTRIBS];
   uint8_t vertex_attribute_formats[MAX_VERTEX_ATTRIBS];
   uint32_t vertex_attribute_bindings[MAX_VERTEX_ATTRIBS];
   uint32_t vertex_attribute_offsets[MAX_VERTEX_ATTRIBS];
   uint32_t vertex_attribute_strides[MAX_VERTEX_ATTRIBS];
   uint8_t vertex_binding_align[MAX_VBS];
   enum ac_fetch_format vertex_alpha_adjust[MAX_VERTEX_ATTRIBS];
   uint32_t vertex_post_shuffle;
   unsigned tess_input_vertices;
   uint32_t col_format;
   uint32_t is_int8;
   uint32_t is_int10;
   uint8_t log2_ps_iter_samples;
   uint8_t num_samples;
   uint32_t has_multiview_view_index : 1;
   uint32_t optimisations_disabled : 1;
   uint32_t provoking_vtx_last : 1;
   uint8_t topology;

   /* Non-zero if a required subgroup size is specified via
    * VK_EXT_subgroup_size_control.
    */
   uint8_t compute_subgroup_size;
   bool require_full_subgroups;
};

struct radv_shader_binary;
struct radv_shader_variant;

void radv_pipeline_cache_init(struct radv_pipeline_cache *cache, struct radv_device *device);
void radv_pipeline_cache_finish(struct radv_pipeline_cache *cache);
bool radv_pipeline_cache_load(struct radv_pipeline_cache *cache, const void *data, size_t size);

bool radv_create_shader_variants_from_pipeline_cache(struct radv_device *device,
                                                     struct radv_pipeline_cache *cache,
                                                     const unsigned char *sha1,
                                                     struct radv_shader_variant **variants,
                                                     bool *found_in_application_cache);

void radv_pipeline_cache_insert_shaders(struct radv_device *device,
                                        struct radv_pipeline_cache *cache,
                                        const unsigned char *sha1,
                                        struct radv_shader_variant **variants,
                                        struct radv_shader_binary *const *binaries);

enum radv_blit_ds_layout {
   RADV_BLIT_DS_LAYOUT_TILE_ENABLE,
   RADV_BLIT_DS_LAYOUT_TILE_DISABLE,
   RADV_BLIT_DS_LAYOUT_COUNT,
};

static inline enum radv_blit_ds_layout
radv_meta_blit_ds_to_type(VkImageLayout layout)
{
   return (layout == VK_IMAGE_LAYOUT_GENERAL) ? RADV_BLIT_DS_LAYOUT_TILE_DISABLE
                                              : RADV_BLIT_DS_LAYOUT_TILE_ENABLE;
}

static inline VkImageLayout
radv_meta_blit_ds_to_layout(enum radv_blit_ds_layout ds_layout)
{
   return ds_layout == RADV_BLIT_DS_LAYOUT_TILE_ENABLE ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_GENERAL;
}

enum radv_meta_dst_layout {
   RADV_META_DST_LAYOUT_GENERAL,
   RADV_META_DST_LAYOUT_OPTIMAL,
   RADV_META_DST_LAYOUT_COUNT,
};

static inline enum radv_meta_dst_layout
radv_meta_dst_layout_from_layout(VkImageLayout layout)
{
   return (layout == VK_IMAGE_LAYOUT_GENERAL) ? RADV_META_DST_LAYOUT_GENERAL
                                              : RADV_META_DST_LAYOUT_OPTIMAL;
}

static inline VkImageLayout
radv_meta_dst_layout_to_layout(enum radv_meta_dst_layout layout)
{
   return layout == RADV_META_DST_LAYOUT_OPTIMAL ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_GENERAL;
}

struct radv_meta_state {
   VkAllocationCallbacks alloc;

   struct radv_pipeline_cache cache;

   /*
    * For on-demand pipeline creation, makes sure that
    * only one thread tries to build a pipeline at the same time.
    */
   mtx_t mtx;

   /**
    * Use array element `i` for images with `2^i` samples.
    */
   struct {
      VkRenderPass render_pass[NUM_META_FS_KEYS];
      VkPipeline color_pipelines[NUM_META_FS_KEYS];

      VkRenderPass depthstencil_rp;
      VkPipeline depth_only_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
      VkPipeline stencil_only_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
      VkPipeline depthstencil_pipeline[NUM_DEPTH_CLEAR_PIPELINES];

      VkPipeline depth_only_unrestricted_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
      VkPipeline stencil_only_unrestricted_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
      VkPipeline depthstencil_unrestricted_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
   } clear[MAX_SAMPLES_LOG2];

   VkPipelineLayout clear_color_p_layout;
   VkPipelineLayout clear_depth_p_layout;
   VkPipelineLayout clear_depth_unrestricted_p_layout;

   /* Optimized compute fast HTILE clear for stencil or depth only. */
   VkPipeline clear_htile_mask_pipeline;
   VkPipelineLayout clear_htile_mask_p_layout;
   VkDescriptorSetLayout clear_htile_mask_ds_layout;

   /* Copy VRS into HTILE. */
   VkPipeline copy_vrs_htile_pipeline;
   VkPipelineLayout copy_vrs_htile_p_layout;
   VkDescriptorSetLayout copy_vrs_htile_ds_layout;

   /* Clear DCC with comp-to-single. */
   VkPipeline clear_dcc_comp_to_single_pipeline[2]; /* 0: 1x, 1: 2x/4x/8x */
   VkPipelineLayout clear_dcc_comp_to_single_p_layout;
   VkDescriptorSetLayout clear_dcc_comp_to_single_ds_layout;

   struct {
      VkRenderPass render_pass[NUM_META_FS_KEYS][RADV_META_DST_LAYOUT_COUNT];

      /** Pipeline that blits from a 1D image. */
      VkPipeline pipeline_1d_src[NUM_META_FS_KEYS];

      /** Pipeline that blits from a 2D image. */
      VkPipeline pipeline_2d_src[NUM_META_FS_KEYS];

      /** Pipeline that blits from a 3D image. */
      VkPipeline pipeline_3d_src[NUM_META_FS_KEYS];

      VkRenderPass depth_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];
      VkPipeline depth_only_1d_pipeline;
      VkPipeline depth_only_2d_pipeline;
      VkPipeline depth_only_3d_pipeline;

      VkRenderPass stencil_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];
      VkPipeline stencil_only_1d_pipeline;
      VkPipeline stencil_only_2d_pipeline;
      VkPipeline stencil_only_3d_pipeline;
      VkPipelineLayout pipeline_layout;
      VkDescriptorSetLayout ds_layout;
   } blit;

   struct {
      VkPipelineLayout p_layouts[5];
      VkDescriptorSetLayout ds_layouts[5];
      VkPipeline pipelines[5][NUM_META_FS_KEYS];

      VkPipeline depth_only_pipeline[5];

      VkPipeline stencil_only_pipeline[5];
   } blit2d[MAX_SAMPLES_LOG2];

   VkRenderPass blit2d_render_passes[NUM_META_FS_KEYS][RADV_META_DST_LAYOUT_COUNT];
   VkRenderPass blit2d_depth_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];
   VkRenderPass blit2d_stencil_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];

   struct {
      VkPipelineLayout img_p_layout;
      VkDescriptorSetLayout img_ds_layout;
      VkPipeline pipeline;
      VkPipeline pipeline_3d;
   } itob;
   struct {
      VkPipelineLayout img_p_layout;
      VkDescriptorSetLayout img_ds_layout;
      VkPipeline pipeline;
      VkPipeline pipeline_3d;
   } btoi;
   struct {
      VkPipelineLayout img_p_layout;
      VkDescriptorSetLayout img_ds_layout;
      VkPipeline pipeline;
   } btoi_r32g32b32;
   struct {
      VkPipelineLayout img_p_layout;
      VkDescriptorSetLayout img_ds_layout;
      VkPipeline pipeline[MAX_SAMPLES_LOG2];
      VkPipeline pipeline_3d;
   } itoi;
   struct {
      VkPipelineLayout img_p_layout;
      VkDescriptorSetLayout img_ds_layout;
      VkPipeline pipeline;
   } itoi_r32g32b32;
   struct {
      VkPipelineLayout img_p_layout;
      VkDescriptorSetLayout img_ds_layout;
      VkPipeline pipeline[MAX_SAMPLES_LOG2];
      VkPipeline pipeline_3d;
   } cleari;
   struct {
      VkPipelineLayout img_p_layout;
      VkDescriptorSetLayout img_ds_layout;
      VkPipeline pipeline;
   } cleari_r32g32b32;

   struct {
      VkPipelineLayout p_layout;
      VkPipeline pipeline[NUM_META_FS_KEYS];
      VkRenderPass pass[NUM_META_FS_KEYS];
   } resolve;

   struct {
      VkDescriptorSetLayout ds_layout;
      VkPipelineLayout p_layout;
      struct {
         VkPipeline pipeline;
         VkPipeline i_pipeline;
         VkPipeline srgb_pipeline;
      } rc[MAX_SAMPLES_LOG2];

      VkPipeline depth_zero_pipeline;
      struct {
         VkPipeline average_pipeline;
         VkPipeline max_pipeline;
         VkPipeline min_pipeline;
      } depth[MAX_SAMPLES_LOG2];

      VkPipeline stencil_zero_pipeline;
      struct {
         VkPipeline max_pipeline;
         VkPipeline min_pipeline;
      } stencil[MAX_SAMPLES_LOG2];
   } resolve_compute;

   struct {
      VkDescriptorSetLayout ds_layout;
      VkPipelineLayout p_layout;

      struct {
         VkRenderPass render_pass[NUM_META_FS_KEYS][RADV_META_DST_LAYOUT_COUNT];
         VkPipeline pipeline[NUM_META_FS_KEYS];
      } rc[MAX_SAMPLES_LOG2];

      VkRenderPass depth_render_pass;
      VkPipeline depth_zero_pipeline;
      struct {
         VkPipeline average_pipeline;
         VkPipeline max_pipeline;
         VkPipeline min_pipeline;
      } depth[MAX_SAMPLES_LOG2];

      VkRenderPass stencil_render_pass;
      VkPipeline stencil_zero_pipeline;
      struct {
         VkPipeline max_pipeline;
         VkPipeline min_pipeline;
      } stencil[MAX_SAMPLES_LOG2];
   } resolve_fragment;

   struct {
      VkPipelineLayout p_layout;
      VkPipeline decompress_pipeline;
      VkPipeline resummarize_pipeline;
      VkRenderPass pass;
   } depth_decomp[MAX_SAMPLES_LOG2];

   struct {
      VkPipelineLayout p_layout;
      VkPipeline cmask_eliminate_pipeline;
      VkPipeline fmask_decompress_pipeline;
      VkPipeline dcc_decompress_pipeline;
      VkRenderPass pass;

      VkDescriptorSetLayout dcc_decompress_compute_ds_layout;
      VkPipelineLayout dcc_decompress_compute_p_layout;
      VkPipeline dcc_decompress_compute_pipeline;
   } fast_clear_flush;

   struct {
      VkPipelineLayout fill_p_layout;
      VkPipelineLayout copy_p_layout;
      VkDescriptorSetLayout fill_ds_layout;
      VkDescriptorSetLayout copy_ds_layout;
      VkPipeline fill_pipeline;
      VkPipeline copy_pipeline;
   } buffer;

   struct {
      VkDescriptorSetLayout ds_layout;
      VkPipelineLayout p_layout;
      VkPipeline occlusion_query_pipeline;
      VkPipeline pipeline_statistics_query_pipeline;
      VkPipeline tfb_query_pipeline;
      VkPipeline timestamp_query_pipeline;
   } query;

   struct {
      VkDescriptorSetLayout ds_layout;
      VkPipelineLayout p_layout;
      VkPipeline pipeline[MAX_SAMPLES_LOG2];
   } fmask_expand;

   struct {
      VkDescriptorSetLayout ds_layout;
      VkPipelineLayout p_layout;
      VkPipeline pipeline[32];
   } dcc_retile;

   struct {
      VkPipelineLayout leaf_p_layout;
      VkPipeline leaf_pipeline;
      VkPipelineLayout internal_p_layout;
      VkPipeline internal_pipeline;
   } accel_struct_build;
};

/* queue types */
#define RADV_QUEUE_GENERAL  0
#define RADV_QUEUE_COMPUTE  1
#define RADV_QUEUE_TRANSFER 2

/* Not a real queue family */
#define RADV_QUEUE_FOREIGN 3

#define RADV_MAX_QUEUE_FAMILIES 3

#define RADV_NUM_HW_CTX (RADEON_CTX_PRIORITY_REALTIME + 1)

struct radv_deferred_queue_submission;

enum ring_type radv_queue_family_to_ring(int f);

struct radv_queue {
   struct vk_object_base base;
   struct radv_device *device;
   struct radeon_winsys_ctx *hw_ctx;
   enum radeon_ctx_priority priority;
   uint32_t queue_family_index;
   int queue_idx;
   VkDeviceQueueCreateFlags flags;

   uint32_t scratch_size_per_wave;
   uint32_t scratch_waves;
   uint32_t compute_scratch_size_per_wave;
   uint32_t compute_scratch_waves;
   uint32_t esgs_ring_size;
   uint32_t gsvs_ring_size;
   bool has_tess_rings;
   bool has_gds;
   bool has_gds_oa;
   bool has_sample_positions;

   struct radeon_winsys_bo *scratch_bo;
   struct radeon_winsys_bo *descriptor_bo;
   struct radeon_winsys_bo *compute_scratch_bo;
   struct radeon_winsys_bo *esgs_ring_bo;
   struct radeon_winsys_bo *gsvs_ring_bo;
   struct radeon_winsys_bo *tess_rings_bo;
   struct radeon_winsys_bo *gds_bo;
   struct radeon_winsys_bo *gds_oa_bo;
   struct radeon_cmdbuf *initial_preamble_cs;
   struct radeon_cmdbuf *initial_full_flush_preamble_cs;
   struct radeon_cmdbuf *continue_preamble_cs;

   struct list_head pending_submissions;
   mtx_t pending_mutex;

   mtx_t thread_mutex;
   struct u_cnd_monotonic thread_cond;
   struct radv_deferred_queue_submission *thread_submission;
   thrd_t submission_thread;
   bool thread_exit;
   bool thread_running;
   bool cond_created;
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

enum radv_force_vrs {
   RADV_FORCE_VRS_NONE = 0,
   RADV_FORCE_VRS_2x2,
   RADV_FORCE_VRS_2x1,
   RADV_FORCE_VRS_1x2,
};

struct radv_device {
   struct vk_device vk;

   struct radv_instance *instance;
   struct radeon_winsys *ws;

   struct radeon_winsys_ctx *hw_ctx[RADV_NUM_HW_CTX];
   struct radv_meta_state meta_state;

   struct radv_queue *queues[RADV_MAX_QUEUE_FAMILIES];
   int queue_count[RADV_MAX_QUEUE_FAMILIES];
   struct radeon_cmdbuf *empty_cs[RADV_MAX_QUEUE_FAMILIES];

   bool pbb_allowed;
   uint32_t tess_offchip_block_dw_size;
   uint32_t scratch_waves;
   uint32_t dispatch_initiator;

   uint32_t gs_table_depth;

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
   uint32_t *trace_id_ptr;

   /* Whether to keep shader debug info, for tracing or VK_AMD_shader_info */
   bool keep_shader_info;

   struct radv_physical_device *physical_device;

   /* Backup in-memory cache to be used if the app doesn't provide one */
   struct radv_pipeline_cache *mem_cache;

   /*
    * use different counters so MSAA MRTs get consecutive surface indices,
    * even if MASK is allocated in between.
    */
   uint32_t image_mrt_offset_counter;
   uint32_t fmask_mrt_offset_counter;
   struct list_head shader_slabs;
   mtx_t shader_slab_mutex;

   /* For detecting VM faults reported by dmesg. */
   uint64_t dmesg_timestamp;

   /* Whether the app has enabled the robustBufferAccess/robustBufferAccess2 features. */
   bool robust_buffer_access;
   bool robust_buffer_access2;

   /* Whether gl_FragCoord.z should be adjusted for VRS due to a hw bug
    * on some GFX10.3 chips.
    */
   bool adjust_frag_coord_z;

   /* Whether the driver uses a global BO list. */
   bool use_global_bo_list;

   /* Whether attachment VRS is enabled. */
   bool attachment_vrs_enabled;

   /* Whether shader image 32-bit float atomics are enabled. */
   bool image_float32_atomics;

   /* Whether anisotropy is forced with RADV_TEX_ANISO (-1 is disabled). */
   int force_aniso;

   struct radv_device_border_color_data border_color_data;

   /* Condition variable for legacy timelines, to notify waiters when a
    * new point gets submitted. */
   struct u_cnd_monotonic timeline_cond;

   /* Thread trace. */
   struct ac_thread_trace_data thread_trace;

   /* Trap handler. */
   struct radv_shader_variant *trap_handler_shader;
   struct radeon_winsys_bo *tma_bo; /* Trap Memory Address */
   uint32_t *tma_ptr;

   /* Overallocation. */
   bool overallocation_disallowed;
   uint64_t allocated_memory_size[VK_MAX_MEMORY_HEAPS];
   mtx_t overallocation_mutex;

   /* Track the number of device loss occurs. */
   int lost;

   /* Whether the user forced VRS rates on GFX10.3+. */
   enum radv_force_vrs force_vrs;

   /* Depth image for VRS when not bound by the app. */
   struct {
      struct radv_image *image;
      struct radv_buffer *buffer; /* HTILE */
      struct radv_device_memory *mem;
   } vrs;
};

VkResult _radv_device_set_lost(struct radv_device *device, const char *file, int line,
                               const char *msg, ...) radv_printflike(4, 5);

#define radv_device_set_lost(dev, ...) _radv_device_set_lost(dev, __FILE__, __LINE__, __VA_ARGS__)

static inline bool
radv_device_is_lost(const struct radv_device *device)
{
   return unlikely(p_atomic_read(&device->lost));
}

struct radv_device_memory {
   struct vk_object_base base;
   struct radeon_winsys_bo *bo;
   /* for dedicated allocations */
   struct radv_image *image;
   struct radv_buffer *buffer;
   uint32_t heap_index;
   uint64_t alloc_size;
   void *map;
   void *user_ptr;

#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
   struct AHardwareBuffer *android_hardware_buffer;
#endif
};

struct radv_descriptor_range {
   uint64_t va;
   uint32_t size;
};

struct radv_descriptor_set_header {
   struct vk_object_base base;
   const struct radv_descriptor_set_layout *layout;
   uint32_t size;
   uint32_t buffer_count;

   struct radeon_winsys_bo *bo;
   uint64_t va;
   uint32_t *mapped_ptr;
   struct radv_descriptor_range *dynamic_descriptors;
};

struct radv_descriptor_set {
   struct radv_descriptor_set_header header;

   struct radeon_winsys_bo *descriptors[];
};

struct radv_push_descriptor_set {
   struct radv_descriptor_set_header set;
   uint32_t capacity;
};

struct radv_descriptor_pool_entry {
   uint32_t offset;
   uint32_t size;
   struct radv_descriptor_set *set;
};

struct radv_descriptor_pool {
   struct vk_object_base base;
   struct radeon_winsys_bo *bo;
   uint8_t *host_bo;
   uint8_t *mapped_ptr;
   uint64_t current_offset;
   uint64_t size;

   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;

   uint32_t entry_count;
   uint32_t max_entry_count;
   struct radv_descriptor_pool_entry entries[0];
};

struct radv_descriptor_update_template_entry {
   VkDescriptorType descriptor_type;

   /* The number of descriptors to update */
   uint32_t descriptor_count;

   /* Into mapped_ptr or dynamic_descriptors, in units of the respective array */
   uint32_t dst_offset;

   /* In dwords. Not valid/used for dynamic descriptors */
   uint32_t dst_stride;

   uint32_t buffer_offset;

   /* Only valid for combined image samplers and samplers */
   uint8_t has_sampler;
   uint8_t sampler_offset;

   /* In bytes */
   size_t src_offset;
   size_t src_stride;

   /* For push descriptors */
   const uint32_t *immutable_samplers;
};

struct radv_descriptor_update_template {
   struct vk_object_base base;
   uint32_t entry_count;
   VkPipelineBindPoint bind_point;
   struct radv_descriptor_update_template_entry entry[0];
};

struct radv_buffer {
   struct vk_object_base base;
   VkDeviceSize size;

   VkBufferUsageFlags usage;
   VkBufferCreateFlags flags;

   /* Set when bound */
   struct radeon_winsys_bo *bo;
   VkDeviceSize offset;

   bool shareable;
};

enum radv_dynamic_state_bits {
   RADV_DYNAMIC_VIEWPORT = 1ull << 0,
   RADV_DYNAMIC_SCISSOR = 1ull << 1,
   RADV_DYNAMIC_LINE_WIDTH = 1ull << 2,
   RADV_DYNAMIC_DEPTH_BIAS = 1ull << 3,
   RADV_DYNAMIC_BLEND_CONSTANTS = 1ull << 4,
   RADV_DYNAMIC_DEPTH_BOUNDS = 1ull << 5,
   RADV_DYNAMIC_STENCIL_COMPARE_MASK = 1ull << 6,
   RADV_DYNAMIC_STENCIL_WRITE_MASK = 1ull << 7,
   RADV_DYNAMIC_STENCIL_REFERENCE = 1ull << 8,
   RADV_DYNAMIC_DISCARD_RECTANGLE = 1ull << 9,
   RADV_DYNAMIC_SAMPLE_LOCATIONS = 1ull << 10,
   RADV_DYNAMIC_LINE_STIPPLE = 1ull << 11,
   RADV_DYNAMIC_CULL_MODE = 1ull << 12,
   RADV_DYNAMIC_FRONT_FACE = 1ull << 13,
   RADV_DYNAMIC_PRIMITIVE_TOPOLOGY = 1ull << 14,
   RADV_DYNAMIC_DEPTH_TEST_ENABLE = 1ull << 15,
   RADV_DYNAMIC_DEPTH_WRITE_ENABLE = 1ull << 16,
   RADV_DYNAMIC_DEPTH_COMPARE_OP = 1ull << 17,
   RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE = 1ull << 18,
   RADV_DYNAMIC_STENCIL_TEST_ENABLE = 1ull << 19,
   RADV_DYNAMIC_STENCIL_OP = 1ull << 20,
   RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE = 1ull << 21,
   RADV_DYNAMIC_FRAGMENT_SHADING_RATE = 1ull << 22,
   RADV_DYNAMIC_PATCH_CONTROL_POINTS = 1ull << 23,
   RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE = 1ull << 24,
   RADV_DYNAMIC_DEPTH_BIAS_ENABLE = 1ull << 25,
   RADV_DYNAMIC_LOGIC_OP = 1ull << 26,
   RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE = 1ull << 27,
   RADV_DYNAMIC_COLOR_WRITE_ENABLE = 1ull << 28,
   RADV_DYNAMIC_ALL = (1ull << 29) - 1,
};

enum radv_cmd_dirty_bits {
   /* Keep the dynamic state dirty bits in sync with
    * enum radv_dynamic_state_bits */
   RADV_CMD_DIRTY_DYNAMIC_VIEWPORT = 1ull << 0,
   RADV_CMD_DIRTY_DYNAMIC_SCISSOR = 1ull << 1,
   RADV_CMD_DIRTY_DYNAMIC_LINE_WIDTH = 1ull << 2,
   RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS = 1ull << 3,
   RADV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS = 1ull << 4,
   RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS = 1ull << 5,
   RADV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK = 1ull << 6,
   RADV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK = 1ull << 7,
   RADV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE = 1ull << 8,
   RADV_CMD_DIRTY_DYNAMIC_DISCARD_RECTANGLE = 1ull << 9,
   RADV_CMD_DIRTY_DYNAMIC_SAMPLE_LOCATIONS = 1ull << 10,
   RADV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE = 1ull << 11,
   RADV_CMD_DIRTY_DYNAMIC_CULL_MODE = 1ull << 12,
   RADV_CMD_DIRTY_DYNAMIC_FRONT_FACE = 1ull << 13,
   RADV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY = 1ull << 14,
   RADV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE = 1ull << 15,
   RADV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE = 1ull << 16,
   RADV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP = 1ull << 17,
   RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE = 1ull << 18,
   RADV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE = 1ull << 19,
   RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP = 1ull << 20,
   RADV_CMD_DIRTY_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE = 1ull << 21,
   RADV_CMD_DIRTY_DYNAMIC_FRAGMENT_SHADING_RATE = 1ull << 22,
   RADV_CMD_DIRTY_DYNAMIC_PATCH_CONTROL_POINTS = 1ull << 23,
   RADV_CMD_DIRTY_DYNAMIC_RASTERIZER_DISCARD_ENABLE = 1ull << 24,
   RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS_ENABLE = 1ull << 25,
   RADV_CMD_DIRTY_DYNAMIC_LOGIC_OP = 1ull << 26,
   RADV_CMD_DIRTY_DYNAMIC_PRIMITIVE_RESTART_ENABLE = 1ull << 27,
   RADV_CMD_DIRTY_DYNAMIC_COLOR_WRITE_ENABLE = 1ull << 28,
   RADV_CMD_DIRTY_DYNAMIC_ALL = (1ull << 29) - 1,
   RADV_CMD_DIRTY_PIPELINE = 1ull << 29,
   RADV_CMD_DIRTY_INDEX_BUFFER = 1ull << 30,
   RADV_CMD_DIRTY_FRAMEBUFFER = 1ull << 31,
   RADV_CMD_DIRTY_VERTEX_BUFFER = 1ull << 32,
   RADV_CMD_DIRTY_STREAMOUT_BUFFER = 1ull << 33
};

enum radv_cmd_flush_bits {
   /* Instruction cache. */
   RADV_CMD_FLAG_INV_ICACHE = 1 << 0,
   /* Scalar L1 cache. */
   RADV_CMD_FLAG_INV_SCACHE = 1 << 1,
   /* Vector L1 cache. */
   RADV_CMD_FLAG_INV_VCACHE = 1 << 2,
   /* L2 cache + L2 metadata cache writeback & invalidate.
    * GFX6-8: Used by shaders only. GFX9-10: Used by everything. */
   RADV_CMD_FLAG_INV_L2 = 1 << 3,
   /* L2 writeback (write dirty L2 lines to memory for non-L2 clients).
    * Only used for coherency with non-L2 clients like CB, DB, CP on GFX6-8.
    * GFX6-7 will do complete invalidation, because the writeback is unsupported. */
   RADV_CMD_FLAG_WB_L2 = 1 << 4,
   /* Invalidate the metadata cache. To be used when the DCC/HTILE metadata
    * changed and we want to read an image from shaders. */
   RADV_CMD_FLAG_INV_L2_METADATA = 1 << 5,
   /* Framebuffer caches */
   RADV_CMD_FLAG_FLUSH_AND_INV_CB_META = 1 << 6,
   RADV_CMD_FLAG_FLUSH_AND_INV_DB_META = 1 << 7,
   RADV_CMD_FLAG_FLUSH_AND_INV_DB = 1 << 8,
   RADV_CMD_FLAG_FLUSH_AND_INV_CB = 1 << 9,
   /* Engine synchronization. */
   RADV_CMD_FLAG_VS_PARTIAL_FLUSH = 1 << 10,
   RADV_CMD_FLAG_PS_PARTIAL_FLUSH = 1 << 11,
   RADV_CMD_FLAG_CS_PARTIAL_FLUSH = 1 << 12,
   RADV_CMD_FLAG_VGT_FLUSH = 1 << 13,
   /* Pipeline query controls. */
   RADV_CMD_FLAG_START_PIPELINE_STATS = 1 << 14,
   RADV_CMD_FLAG_STOP_PIPELINE_STATS = 1 << 15,
   RADV_CMD_FLAG_VGT_STREAMOUT_SYNC = 1 << 16,

   RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER =
      (RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_CB_META |
       RADV_CMD_FLAG_FLUSH_AND_INV_DB | RADV_CMD_FLAG_FLUSH_AND_INV_DB_META)
};

struct radv_vertex_binding {
   struct radv_buffer *buffer;
   VkDeviceSize offset;
   VkDeviceSize size;
   VkDeviceSize stride;
};

struct radv_streamout_binding {
   struct radv_buffer *buffer;
   VkDeviceSize offset;
   VkDeviceSize size;
};

struct radv_streamout_state {
   /* Mask of bound streamout buffers. */
   uint8_t enabled_mask;

   /* External state that comes from the last vertex stage, it must be
    * set explicitely when binding a new graphics pipeline.
    */
   uint16_t stride_in_dw[MAX_SO_BUFFERS];
   uint32_t enabled_stream_buffers_mask; /* stream0 buffers0-3 in 4 LSB */

   /* State of VGT_STRMOUT_BUFFER_(CONFIG|END) */
   uint32_t hw_enabled_mask;

   /* State of VGT_STRMOUT_(CONFIG|EN) */
   bool streamout_enabled;
};

struct radv_viewport_state {
   uint32_t count;
   VkViewport viewports[MAX_VIEWPORTS];
   struct {
      float scale[3];
      float translate[3];
   } xform[MAX_VIEWPORTS];
};

struct radv_scissor_state {
   uint32_t count;
   VkRect2D scissors[MAX_SCISSORS];
};

struct radv_discard_rectangle_state {
   uint32_t count;
   VkRect2D rectangles[MAX_DISCARD_RECTANGLES];
};

struct radv_sample_locations_state {
   VkSampleCountFlagBits per_pixel;
   VkExtent2D grid_size;
   uint32_t count;
   VkSampleLocationEXT locations[MAX_SAMPLE_LOCATIONS];
};

struct radv_dynamic_state {
   /**
    * Bitmask of (1ull << VK_DYNAMIC_STATE_*).
    * Defines the set of saved dynamic state.
    */
   uint64_t mask;

   struct radv_viewport_state viewport;

   struct radv_scissor_state scissor;

   float line_width;

   struct {
      float bias;
      float clamp;
      float slope;
   } depth_bias;

   float blend_constants[4];

   struct {
      float min;
      float max;
   } depth_bounds;

   struct {
      uint32_t front;
      uint32_t back;
   } stencil_compare_mask;

   struct {
      uint32_t front;
      uint32_t back;
   } stencil_write_mask;

   struct {
      struct {
         VkStencilOp fail_op;
         VkStencilOp pass_op;
         VkStencilOp depth_fail_op;
         VkCompareOp compare_op;
      } front;

      struct {
         VkStencilOp fail_op;
         VkStencilOp pass_op;
         VkStencilOp depth_fail_op;
         VkCompareOp compare_op;
      } back;
   } stencil_op;

   struct {
      uint32_t front;
      uint32_t back;
   } stencil_reference;

   struct radv_discard_rectangle_state discard_rectangle;

   struct radv_sample_locations_state sample_location;

   struct {
      uint32_t factor;
      uint16_t pattern;
   } line_stipple;

   VkCullModeFlags cull_mode;
   VkFrontFace front_face;
   unsigned primitive_topology;

   bool depth_test_enable;
   bool depth_write_enable;
   VkCompareOp depth_compare_op;
   bool depth_bounds_test_enable;
   bool stencil_test_enable;

   struct {
      VkExtent2D size;
      VkFragmentShadingRateCombinerOpKHR combiner_ops[2];
   } fragment_shading_rate;

   bool depth_bias_enable;
   bool primitive_restart_enable;
   bool rasterizer_discard_enable;

   unsigned logic_op;

   uint32_t color_write_enable;
};

extern const struct radv_dynamic_state default_dynamic_state;

const char *radv_get_debug_option_name(int id);

const char *radv_get_perftest_option_name(int id);

int radv_get_int_debug_option(const char *name, int default_value);

struct radv_color_buffer_info {
   uint64_t cb_color_base;
   uint64_t cb_color_cmask;
   uint64_t cb_color_fmask;
   uint64_t cb_dcc_base;
   uint32_t cb_color_slice;
   uint32_t cb_color_view;
   uint32_t cb_color_info;
   uint32_t cb_color_attrib;
   uint32_t cb_color_attrib2; /* GFX9 and later */
   uint32_t cb_color_attrib3; /* GFX10 and later */
   uint32_t cb_dcc_control;
   uint32_t cb_color_cmask_slice;
   uint32_t cb_color_fmask_slice;
   union {
      uint32_t cb_color_pitch; // GFX6-GFX8
      uint32_t cb_mrt_epitch;  // GFX9+
   };
};

struct radv_ds_buffer_info {
   uint64_t db_z_read_base;
   uint64_t db_stencil_read_base;
   uint64_t db_z_write_base;
   uint64_t db_stencil_write_base;
   uint64_t db_htile_data_base;
   uint32_t db_depth_info;
   uint32_t db_z_info;
   uint32_t db_stencil_info;
   uint32_t db_depth_view;
   uint32_t db_depth_size;
   uint32_t db_depth_slice;
   uint32_t db_htile_surface;
   uint32_t pa_su_poly_offset_db_fmt_cntl;
   uint32_t db_z_info2;       /* GFX9 only */
   uint32_t db_stencil_info2; /* GFX9 only */
};

void radv_initialise_color_surface(struct radv_device *device, struct radv_color_buffer_info *cb,
                                   struct radv_image_view *iview);
void radv_initialise_ds_surface(struct radv_device *device, struct radv_ds_buffer_info *ds,
                                struct radv_image_view *iview);
void radv_initialise_vrs_surface(struct radv_image *image, struct radv_buffer *htile_buffer,
                                 struct radv_ds_buffer_info *ds);

/**
 * Attachment state when recording a renderpass instance.
 *
 * The clear value is valid only if there exists a pending clear.
 */
struct radv_attachment_state {
   VkImageAspectFlags pending_clear_aspects;
   uint32_t cleared_views;
   VkClearValue clear_value;
   VkImageLayout current_layout;
   VkImageLayout current_stencil_layout;
   bool current_in_render_loop;
   bool disable_dcc;
   struct radv_sample_locations_state sample_location;

   union {
      struct radv_color_buffer_info cb;
      struct radv_ds_buffer_info ds;
   };
   struct radv_image_view *iview;
};

struct radv_descriptor_state {
   struct radv_descriptor_set *sets[MAX_SETS];
   uint32_t dirty;
   uint32_t valid;
   struct radv_push_descriptor_set push_set;
   bool push_dirty;
   uint32_t dynamic_buffers[4 * MAX_DYNAMIC_BUFFERS];
};

struct radv_subpass_sample_locs_state {
   uint32_t subpass_idx;
   struct radv_sample_locations_state sample_location;
};

enum rgp_flush_bits {
   RGP_FLUSH_WAIT_ON_EOP_TS = 0x1,
   RGP_FLUSH_VS_PARTIAL_FLUSH = 0x2,
   RGP_FLUSH_PS_PARTIAL_FLUSH = 0x4,
   RGP_FLUSH_CS_PARTIAL_FLUSH = 0x8,
   RGP_FLUSH_PFP_SYNC_ME = 0x10,
   RGP_FLUSH_SYNC_CP_DMA = 0x20,
   RGP_FLUSH_INVAL_VMEM_L0 = 0x40,
   RGP_FLUSH_INVAL_ICACHE = 0x80,
   RGP_FLUSH_INVAL_SMEM_L0 = 0x100,
   RGP_FLUSH_FLUSH_L2 = 0x200,
   RGP_FLUSH_INVAL_L2 = 0x400,
   RGP_FLUSH_FLUSH_CB = 0x800,
   RGP_FLUSH_INVAL_CB = 0x1000,
   RGP_FLUSH_FLUSH_DB = 0x2000,
   RGP_FLUSH_INVAL_DB = 0x4000,
   RGP_FLUSH_INVAL_L1 = 0x8000,
};

struct radv_cmd_state {
   /* Vertex descriptors */
   uint64_t vb_va;

   bool predicating;
   uint64_t dirty;

   uint32_t prefetch_L2_mask;

   struct radv_pipeline *pipeline;
   struct radv_pipeline *emitted_pipeline;
   struct radv_pipeline *compute_pipeline;
   struct radv_pipeline *emitted_compute_pipeline;
   struct radv_pipeline *rt_pipeline; /* emitted = emitted_compute_pipeline */
   struct radv_framebuffer *framebuffer;
   struct radv_render_pass *pass;
   const struct radv_subpass *subpass;
   struct radv_dynamic_state dynamic;
   struct radv_attachment_state *attachments;
   struct radv_streamout_state streamout;
   VkRect2D render_area;

   uint32_t num_subpass_sample_locs;
   struct radv_subpass_sample_locs_state *subpass_sample_locs;

   /* Index buffer */
   struct radv_buffer *index_buffer;
   uint64_t index_offset;
   uint32_t index_type;
   uint32_t max_index_count;
   uint64_t index_va;
   int32_t last_index_type;

   int32_t last_primitive_reset_en;
   uint32_t last_primitive_reset_index;
   enum radv_cmd_flush_bits flush_bits;
   unsigned active_occlusion_queries;
   bool perfect_occlusion_queries_enabled;
   unsigned active_pipeline_queries;
   unsigned active_pipeline_gds_queries;
   uint32_t trace_id;
   uint32_t last_ia_multi_vgt_param;

   uint32_t last_num_instances;
   uint32_t last_first_instance;
   uint32_t last_vertex_offset;
   uint32_t last_drawid;

   uint32_t last_sx_ps_downconvert;
   uint32_t last_sx_blend_opt_epsilon;
   uint32_t last_sx_blend_opt_control;

   /* Whether CP DMA is busy/idle. */
   bool dma_is_busy;

   /* Conditional rendering info. */
   uint8_t predication_op; /* 32-bit or 64-bit predicate value */
   int predication_type;   /* -1: disabled, 0: normal, 1: inverted */
   uint64_t predication_va;

   /* Inheritance info. */
   VkQueryPipelineStatisticFlags inherited_pipeline_statistics;

   bool context_roll_without_scissor_emitted;

   /* SQTT related state. */
   uint32_t current_event_type;
   uint32_t num_events;
   uint32_t num_layout_transitions;
   bool pending_sqtt_barrier_end;
   enum rgp_flush_bits sqtt_flush_bits;

   /* NGG culling state. */
   uint32_t last_nggc_settings;
   int8_t last_nggc_settings_sgpr_idx;
   bool last_nggc_skip;

   uint8_t cb_mip[MAX_RTS];

   /* Whether DRAW_{INDEX}_INDIRECT_MULTI is emitted. */
   bool uses_draw_indirect_multi;
};

struct radv_cmd_pool {
   struct vk_object_base base;
   VkAllocationCallbacks alloc;
   struct list_head cmd_buffers;
   struct list_head free_cmd_buffers;
   uint32_t queue_family_index;
};

struct radv_cmd_buffer_upload {
   uint8_t *map;
   unsigned offset;
   uint64_t size;
   struct radeon_winsys_bo *upload_bo;
   struct list_head list;
};

enum radv_cmd_buffer_status {
   RADV_CMD_BUFFER_STATUS_INVALID,
   RADV_CMD_BUFFER_STATUS_INITIAL,
   RADV_CMD_BUFFER_STATUS_RECORDING,
   RADV_CMD_BUFFER_STATUS_EXECUTABLE,
   RADV_CMD_BUFFER_STATUS_PENDING,
};

struct radv_cmd_buffer {
   struct vk_object_base base;

   struct radv_device *device;

   struct radv_cmd_pool *pool;
   struct list_head pool_link;

   VkCommandBufferUsageFlags usage_flags;
   VkCommandBufferLevel level;
   enum radv_cmd_buffer_status status;
   struct radeon_cmdbuf *cs;
   struct radv_cmd_state state;
   struct radv_vertex_binding vertex_bindings[MAX_VBS];
   struct radv_streamout_binding streamout_bindings[MAX_SO_BUFFERS];
   uint32_t queue_family_index;

   uint8_t push_constants[MAX_PUSH_CONSTANTS_SIZE];
   VkShaderStageFlags push_constant_stages;
   struct radv_descriptor_set_header meta_push_descriptors;

   struct radv_descriptor_state descriptors[MAX_BIND_POINTS];

   struct radv_cmd_buffer_upload upload;

   uint32_t scratch_size_per_wave_needed;
   uint32_t scratch_waves_wanted;
   uint32_t compute_scratch_size_per_wave_needed;
   uint32_t compute_scratch_waves_wanted;
   uint32_t esgs_ring_size_needed;
   uint32_t gsvs_ring_size_needed;
   bool tess_rings_needed;
   bool gds_needed;    /* for GFX10 streamout and NGG GS queries */
   bool gds_oa_needed; /* for GFX10 streamout */
   bool sample_positions_needed;

   VkResult record_result;

   uint64_t gfx9_fence_va;
   uint32_t gfx9_fence_idx;
   uint64_t gfx9_eop_bug_va;

   /**
    * Whether a query pool has been resetted and we have to flush caches.
    */
   bool pending_reset_query;

   /**
    * Bitmask of pending active query flushes.
    */
   enum radv_cmd_flush_bits active_query_flush_bits;
};

struct radv_image;
struct radv_image_view;

bool radv_cmd_buffer_uses_mec(struct radv_cmd_buffer *cmd_buffer);

void si_emit_graphics(struct radv_device *device, struct radeon_cmdbuf *cs);
void si_emit_compute(struct radv_device *device, struct radeon_cmdbuf *cs);

void cik_create_gfx_config(struct radv_device *device);

void si_write_scissors(struct radeon_cmdbuf *cs, int first, int count, const VkRect2D *scissors,
                       const VkViewport *viewports, bool can_use_guardband);
uint32_t si_get_ia_multi_vgt_param(struct radv_cmd_buffer *cmd_buffer, bool instanced_draw,
                                   bool indirect_draw, bool count_from_stream_output,
                                   uint32_t draw_vertex_count, unsigned topology,
                                   bool prim_restart_enable);
void si_cs_emit_write_event_eop(struct radeon_cmdbuf *cs, enum chip_class chip_class, bool is_mec,
                                unsigned event, unsigned event_flags, unsigned dst_sel,
                                unsigned data_sel, uint64_t va, uint32_t new_fence,
                                uint64_t gfx9_eop_bug_va);

void radv_cp_wait_mem(struct radeon_cmdbuf *cs, uint32_t op, uint64_t va, uint32_t ref,
                      uint32_t mask);
void si_cs_emit_cache_flush(struct radeon_cmdbuf *cs, enum chip_class chip_class,
                            uint32_t *fence_ptr, uint64_t va, bool is_mec,
                            enum radv_cmd_flush_bits flush_bits,
                            enum rgp_flush_bits *sqtt_flush_bits, uint64_t gfx9_eop_bug_va);
void si_emit_cache_flush(struct radv_cmd_buffer *cmd_buffer);
void si_emit_set_predication_state(struct radv_cmd_buffer *cmd_buffer, bool draw_visible,
                                   unsigned pred_op, uint64_t va);
void si_cp_dma_buffer_copy(struct radv_cmd_buffer *cmd_buffer, uint64_t src_va, uint64_t dest_va,
                           uint64_t size);
void si_cp_dma_prefetch(struct radv_cmd_buffer *cmd_buffer, uint64_t va, unsigned size);
void si_cp_dma_clear_buffer(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size,
                            unsigned value);
void si_cp_dma_wait_for_idle(struct radv_cmd_buffer *cmd_buffer);

void radv_set_db_count_control(struct radv_cmd_buffer *cmd_buffer);
bool radv_cmd_buffer_upload_alloc(struct radv_cmd_buffer *cmd_buffer, unsigned size,
                                  unsigned *out_offset, void **ptr);
void radv_cmd_buffer_set_subpass(struct radv_cmd_buffer *cmd_buffer,
                                 const struct radv_subpass *subpass);
bool radv_cmd_buffer_upload_data(struct radv_cmd_buffer *cmd_buffer, unsigned size,
                                 const void *data, unsigned *out_offset);

void radv_cmd_buffer_clear_subpass(struct radv_cmd_buffer *cmd_buffer);
void radv_cmd_buffer_resolve_subpass(struct radv_cmd_buffer *cmd_buffer);
void radv_cmd_buffer_resolve_subpass_cs(struct radv_cmd_buffer *cmd_buffer);
void radv_depth_stencil_resolve_subpass_cs(struct radv_cmd_buffer *cmd_buffer,
                                           VkImageAspectFlags aspects,
                                           VkResolveModeFlagBits resolve_mode);
void radv_cmd_buffer_resolve_subpass_fs(struct radv_cmd_buffer *cmd_buffer);
void radv_depth_stencil_resolve_subpass_fs(struct radv_cmd_buffer *cmd_buffer,
                                           VkImageAspectFlags aspects,
                                           VkResolveModeFlagBits resolve_mode);
void radv_emit_default_sample_locations(struct radeon_cmdbuf *cs, int nr_samples);
unsigned radv_get_default_max_sample_dist(int log_samples);
void radv_device_init_msaa(struct radv_device *device);
VkResult radv_device_init_vrs_state(struct radv_device *device);

void radv_update_ds_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
                                   const struct radv_image_view *iview,
                                   VkClearDepthStencilValue ds_clear_value,
                                   VkImageAspectFlags aspects);

void radv_update_color_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
                                      const struct radv_image_view *iview, int cb_idx,
                                      uint32_t color_values[2]);

bool radv_image_use_dcc_image_stores(const struct radv_device *device,
                                     const struct radv_image *image);
bool radv_image_use_dcc_predication(const struct radv_device *device,
                                    const struct radv_image *image);

void radv_update_fce_metadata(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                              const VkImageSubresourceRange *range, bool value);

void radv_update_dcc_metadata(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                              const VkImageSubresourceRange *range, bool value);
enum radv_cmd_flush_bits radv_src_access_flush(struct radv_cmd_buffer *cmd_buffer,
                                               VkAccessFlags src_flags,
                                               const struct radv_image *image);
enum radv_cmd_flush_bits radv_dst_access_flush(struct radv_cmd_buffer *cmd_buffer,
                                               VkAccessFlags dst_flags,
                                               const struct radv_image *image);
uint32_t radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image,
                          struct radeon_winsys_bo *bo, uint64_t offset, uint64_t size,
                          uint32_t value);
void radv_cmd_buffer_trace_emit(struct radv_cmd_buffer *cmd_buffer);
bool radv_get_memory_fd(struct radv_device *device, struct radv_device_memory *memory, int *pFD);
void radv_free_memory(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                      struct radv_device_memory *mem);

static inline void
radv_emit_shader_pointer_head(struct radeon_cmdbuf *cs, unsigned sh_offset, unsigned pointer_count,
                              bool use_32bit_pointers)
{
   radeon_emit(cs, PKT3(PKT3_SET_SH_REG, pointer_count * (use_32bit_pointers ? 1 : 2), 0));
   radeon_emit(cs, (sh_offset - SI_SH_REG_OFFSET) >> 2);
}

static inline void
radv_emit_shader_pointer_body(struct radv_device *device, struct radeon_cmdbuf *cs, uint64_t va,
                              bool use_32bit_pointers)
{
   radeon_emit(cs, va);

   if (use_32bit_pointers) {
      assert(va == 0 || (va >> 32) == device->physical_device->rad_info.address32_hi);
   } else {
      radeon_emit(cs, va >> 32);
   }
}

static inline void
radv_emit_shader_pointer(struct radv_device *device, struct radeon_cmdbuf *cs, uint32_t sh_offset,
                         uint64_t va, bool global)
{
   bool use_32bit_pointers = !global;

   radv_emit_shader_pointer_head(cs, sh_offset, 1, use_32bit_pointers);
   radv_emit_shader_pointer_body(device, cs, va, use_32bit_pointers);
}

static inline struct radv_descriptor_state *
radv_get_descriptors_state(struct radv_cmd_buffer *cmd_buffer, VkPipelineBindPoint bind_point)
{
   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmd_buffer->descriptors[bind_point];
   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
      return &cmd_buffer->descriptors[2];
   default:
      unreachable("Unhandled bind point");
   }
}

void
radv_get_viewport_xform(const VkViewport *viewport, float scale[3], float translate[3]);

/*
 * Takes x,y,z as exact numbers of invocations, instead of blocks.
 *
 * Limitations: Can't call normal dispatch functions without binding or rebinding
 *              the compute pipeline.
 */
void radv_unaligned_dispatch(struct radv_cmd_buffer *cmd_buffer, uint32_t x, uint32_t y,
                             uint32_t z);

struct radv_event {
   struct vk_object_base base;
   struct radeon_winsys_bo *bo;
   uint64_t *map;
};

#define RADV_HASH_SHADER_NO_NGG            (1 << 0)
#define RADV_HASH_SHADER_CS_WAVE32         (1 << 1)
#define RADV_HASH_SHADER_PS_WAVE32         (1 << 2)
#define RADV_HASH_SHADER_GE_WAVE32         (1 << 3)
#define RADV_HASH_SHADER_LLVM              (1 << 4)
#define RADV_HASH_SHADER_DISCARD_TO_DEMOTE (1 << 5)
#define RADV_HASH_SHADER_MRT_NAN_FIXUP     (1 << 6)
#define RADV_HASH_SHADER_INVARIANT_GEOM    (1 << 7)
#define RADV_HASH_SHADER_KEEP_STATISTICS   (1 << 8)
#define RADV_HASH_SHADER_FORCE_VRS_2x2     (1 << 9)
#define RADV_HASH_SHADER_FORCE_VRS_2x1     (1 << 10)
#define RADV_HASH_SHADER_FORCE_VRS_1x2     (1 << 11)
#define RADV_HASH_SHADER_FORCE_NGG_CULLING (1 << 13)
#define RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS (1 << 14)
#define RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS2 (1 << 15)

void radv_hash_shaders(unsigned char *hash, const VkPipelineShaderStageCreateInfo **stages,
                       const struct radv_pipeline_layout *layout,
                       const struct radv_pipeline_key *key, uint32_t flags);

#define RADV_STAGE_MASK ((1 << MESA_SHADER_STAGES) - 1)

#define radv_foreach_stage(stage, stage_bits)                                                      \
   for (gl_shader_stage stage, __tmp = (gl_shader_stage)((stage_bits)&RADV_STAGE_MASK);            \
        stage = ffs(__tmp) - 1, __tmp; __tmp &= ~(1 << (stage)))

extern const VkFormat radv_fs_key_format_exemplars[NUM_META_FS_KEYS];
unsigned radv_format_meta_fs_key(struct radv_device *device, VkFormat format);

struct radv_multisample_state {
   uint32_t db_eqaa;
   uint32_t pa_sc_mode_cntl_0;
   uint32_t pa_sc_mode_cntl_1;
   uint32_t pa_sc_aa_config;
   uint32_t pa_sc_aa_mask[2];
   unsigned num_samples;
};

struct radv_vrs_state {
   uint32_t pa_cl_vrs_cntl;
};

struct radv_prim_vertex_count {
   uint8_t min;
   uint8_t incr;
};

struct radv_ia_multi_vgt_param_helpers {
   uint32_t base;
   bool partial_es_wave;
   uint8_t primgroup_size;
   bool ia_switch_on_eoi;
   bool partial_vs_wave;
};

struct radv_binning_state {
   uint32_t pa_sc_binner_cntl_0;
};

#define SI_GS_PER_ES 128

struct radv_pipeline {
   struct vk_object_base base;
   struct radv_device *device;
   struct radv_dynamic_state dynamic_state;

   struct radv_pipeline_layout *layout;

   bool need_indirect_descriptor_sets;
   struct radv_shader_variant *shaders[MESA_SHADER_STAGES];
   struct radv_shader_variant *gs_copy_shader;
   VkShaderStageFlags active_stages;

   struct radeon_cmdbuf cs;
   uint32_t ctx_cs_hash;
   struct radeon_cmdbuf ctx_cs;

   uint32_t binding_stride[MAX_VBS];

   uint8_t attrib_bindings[MAX_VERTEX_ATTRIBS];
   uint32_t attrib_ends[MAX_VERTEX_ATTRIBS];
   uint32_t attrib_index_offset[MAX_VERTEX_ATTRIBS];

   bool use_per_attribute_vb_descs;
   uint32_t vb_desc_usage_mask;
   uint32_t vb_desc_alloc_size;

   uint32_t user_data_0[MESA_SHADER_STAGES];
   union {
      struct {
         struct radv_multisample_state ms;
         struct radv_binning_state binning;
         struct radv_vrs_state vrs;
         uint32_t spi_baryc_cntl;
         unsigned esgs_ring_size;
         unsigned gsvs_ring_size;
         uint32_t vtx_base_sgpr;
         struct radv_ia_multi_vgt_param_helpers ia_multi_vgt_param;
         uint8_t vtx_emit_num;
         bool uses_drawid;
         bool uses_baseinstance;
         bool can_use_guardband;
         uint64_t needed_dynamic_state;
         bool disable_out_of_order_rast_for_occlusion;
         unsigned tess_patch_control_points;
         unsigned pa_su_sc_mode_cntl;
         unsigned db_depth_control;
         unsigned pa_cl_clip_cntl;
         unsigned cb_color_control;
         bool uses_dynamic_stride;
         bool uses_conservative_overestimate;

         /* Used for rbplus */
         uint32_t col_format;
         uint32_t cb_target_mask;

         /* Whether the pipeline uses NGG (GFX10+). */
         bool is_ngg;
         bool has_ngg_culling;

         /* Last pre-PS API stage */
         gl_shader_stage last_vgt_api_stage;
      } graphics;
   };

   unsigned max_waves;
   unsigned scratch_bytes_per_wave;

   /* Not NULL if graphics pipeline uses streamout. */
   struct radv_shader_variant *streamout_shader;

   /* Unique pipeline hash identifier. */
   uint64_t pipeline_hash;
};

static inline bool
radv_pipeline_has_gs(const struct radv_pipeline *pipeline)
{
   return pipeline->shaders[MESA_SHADER_GEOMETRY] ? true : false;
}

static inline bool
radv_pipeline_has_tess(const struct radv_pipeline *pipeline)
{
   return pipeline->shaders[MESA_SHADER_TESS_CTRL] ? true : false;
}

bool radv_pipeline_has_ngg_passthrough(const struct radv_pipeline *pipeline);

bool radv_pipeline_has_gs_copy_shader(const struct radv_pipeline *pipeline);

struct radv_userdata_info *radv_lookup_user_sgpr(struct radv_pipeline *pipeline,
                                                 gl_shader_stage stage, int idx);

struct radv_shader_variant *radv_get_shader(const struct radv_pipeline *pipeline,
                                            gl_shader_stage stage);

struct radv_graphics_pipeline_create_info {
   bool use_rectlist;
   bool db_depth_clear;
   bool db_stencil_clear;
   bool depth_compress_disable;
   bool stencil_compress_disable;
   bool resummarize_enable;
   uint32_t custom_blend_mode;
};

VkResult radv_graphics_pipeline_create(VkDevice device, VkPipelineCache cache,
                                       const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                       const struct radv_graphics_pipeline_create_info *extra,
                                       const VkAllocationCallbacks *alloc, VkPipeline *pPipeline);

struct radv_binning_settings {
   unsigned context_states_per_bin;    /* allowed range: [1, 6] */
   unsigned persistent_states_per_bin; /* allowed range: [1, 32] */
   unsigned fpovs_per_batch;           /* allowed range: [0, 255], 0 = unlimited */
};

struct radv_binning_settings radv_get_binning_settings(const struct radv_physical_device *pdev);

struct vk_format_description;
uint32_t radv_translate_buffer_dataformat(const struct util_format_description *desc,
                                          int first_non_void);
uint32_t radv_translate_buffer_numformat(const struct util_format_description *desc,
                                         int first_non_void);
bool radv_is_buffer_format_supported(VkFormat format, bool *scaled);
uint32_t radv_translate_colorformat(VkFormat format);
uint32_t radv_translate_color_numformat(VkFormat format, const struct util_format_description *desc,
                                        int first_non_void);
uint32_t radv_colorformat_endian_swap(uint32_t colorformat);
unsigned radv_translate_colorswap(VkFormat format, bool do_endian_swap);
uint32_t radv_translate_dbformat(VkFormat format);
uint32_t radv_translate_tex_dataformat(VkFormat format, const struct util_format_description *desc,
                                       int first_non_void);
uint32_t radv_translate_tex_numformat(VkFormat format, const struct util_format_description *desc,
                                      int first_non_void);
bool radv_format_pack_clear_color(VkFormat format, uint32_t clear_vals[2],
                                  VkClearColorValue *value);
bool radv_is_storage_image_format_supported(struct radv_physical_device *physical_device,
                                            VkFormat format);
bool radv_is_colorbuffer_format_supported(const struct radv_physical_device *pdevice,
                                          VkFormat format, bool *blendable);
bool radv_dcc_formats_compatible(VkFormat format1, VkFormat format2, bool *sign_reinterpret);
bool radv_is_atomic_format_supported(VkFormat format);
bool radv_device_supports_etc(struct radv_physical_device *physical_device);

struct radv_image_plane {
   VkFormat format;
   struct radeon_surf surface;
};

struct radv_image {
   struct vk_object_base base;
   VkImageType type;
   /* The original VkFormat provided by the client.  This may not match any
    * of the actual surface formats.
    */
   VkFormat vk_format;
   VkImageUsageFlags usage; /**< Superset of VkImageCreateInfo::usage. */
   struct ac_surf_info info;
   VkImageTiling tiling;     /** VkImageCreateInfo::tiling */
   VkImageCreateFlags flags; /** VkImageCreateInfo::flags */

   VkDeviceSize size;
   uint32_t alignment;

   unsigned queue_family_mask;
   bool exclusive;
   bool shareable;
   bool l2_coherent;
   bool dcc_sign_reinterpret;
   bool support_comp_to_single;

   /* Set when bound */
   struct radeon_winsys_bo *bo;
   VkDeviceSize offset;
   bool tc_compatible_cmask;

   uint64_t clear_value_offset;
   uint64_t fce_pred_offset;
   uint64_t dcc_pred_offset;

   /*
    * Metadata for the TC-compat zrange workaround. If the 32-bit value
    * stored at this offset is UINT_MAX, the driver will emit
    * DB_Z_INFO.ZRANGE_PRECISION=0, otherwise it will skip the
    * SET_CONTEXT_REG packet.
    */
   uint64_t tc_compat_zrange_offset;

   /* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
   VkDeviceMemory owned_memory;

   unsigned plane_count;
   struct radv_image_plane planes[0];
};

/* Whether the image has a htile  that is known consistent with the contents of
 * the image and is allowed to be in compressed form.
 *
 * If this is false reads that don't use the htile should be able to return
 * correct results.
 */
bool radv_layout_is_htile_compressed(const struct radv_device *device,
                                     const struct radv_image *image, VkImageLayout layout,
                                     bool in_render_loop, unsigned queue_mask);

bool radv_layout_can_fast_clear(const struct radv_device *device, const struct radv_image *image,
                                unsigned level, VkImageLayout layout, bool in_render_loop,
                                unsigned queue_mask);

bool radv_layout_dcc_compressed(const struct radv_device *device, const struct radv_image *image,
                                unsigned level, VkImageLayout layout, bool in_render_loop,
                                unsigned queue_mask);

bool radv_layout_fmask_compressed(const struct radv_device *device, const struct radv_image *image,
                                  VkImageLayout layout, unsigned queue_mask);

/**
 * Return whether the image has CMASK metadata for color surfaces.
 */
static inline bool
radv_image_has_cmask(const struct radv_image *image)
{
   return image->planes[0].surface.cmask_offset;
}

/**
 * Return whether the image has FMASK metadata for color surfaces.
 */
static inline bool
radv_image_has_fmask(const struct radv_image *image)
{
   return image->planes[0].surface.fmask_offset;
}

/**
 * Return whether the image has DCC metadata for color surfaces.
 */
static inline bool
radv_image_has_dcc(const struct radv_image *image)
{
   return !(image->planes[0].surface.flags & RADEON_SURF_Z_OR_SBUFFER) &&
          image->planes[0].surface.meta_offset;
}

/**
 * Return whether the image is TC-compatible CMASK.
 */
static inline bool
radv_image_is_tc_compat_cmask(const struct radv_image *image)
{
   return radv_image_has_fmask(image) && image->tc_compatible_cmask;
}

/**
 * Return whether DCC metadata is enabled for a level.
 */
static inline bool
radv_dcc_enabled(const struct radv_image *image, unsigned level)
{
   return radv_image_has_dcc(image) && level < image->planes[0].surface.num_meta_levels;
}

/**
 * Return whether the image has CB metadata.
 */
static inline bool
radv_image_has_CB_metadata(const struct radv_image *image)
{
   return radv_image_has_cmask(image) || radv_image_has_fmask(image) || radv_image_has_dcc(image);
}

/**
 * Return whether the image has HTILE metadata for depth surfaces.
 */
static inline bool
radv_image_has_htile(const struct radv_image *image)
{
   return image->planes[0].surface.flags & RADEON_SURF_Z_OR_SBUFFER &&
          image->planes[0].surface.meta_size;
}

/**
 * Return whether the image has VRS HTILE metadata for depth surfaces
 */
static inline bool
radv_image_has_vrs_htile(const struct radv_device *device, const struct radv_image *image)
{
   /* Any depth buffer can potentially use VRS. */
   return device->attachment_vrs_enabled && radv_image_has_htile(image) &&
          (image->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

/**
 * Return whether HTILE metadata is enabled for a level.
 */
static inline bool
radv_htile_enabled(const struct radv_image *image, unsigned level)
{
   return radv_image_has_htile(image) && level < image->planes[0].surface.num_meta_levels;
}

/**
 * Return whether the image is TC-compatible HTILE.
 */
static inline bool
radv_image_is_tc_compat_htile(const struct radv_image *image)
{
   return radv_image_has_htile(image) &&
          (image->planes[0].surface.flags & RADEON_SURF_TC_COMPATIBLE_HTILE);
}

/**
 * Return whether the entire HTILE buffer can be used for depth in order to
 * improve HiZ Z-Range precision.
 */
static inline bool
radv_image_tile_stencil_disabled(const struct radv_device *device, const struct radv_image *image)
{
   if (device->physical_device->rad_info.chip_class >= GFX9) {
      return !vk_format_has_stencil(image->vk_format) && !radv_image_has_vrs_htile(device, image);
   } else {
      /* Due to a hw bug, TILE_STENCIL_DISABLE must be set to 0 for
       * the TC-compat ZRANGE issue even if no stencil is used.
       */
      return !vk_format_has_stencil(image->vk_format) && !radv_image_is_tc_compat_htile(image);
   }
}

static inline bool
radv_image_has_clear_value(const struct radv_image *image)
{
   return image->clear_value_offset != 0;
}

static inline uint64_t
radv_image_get_fast_clear_va(const struct radv_image *image, uint32_t base_level)
{
   assert(radv_image_has_clear_value(image));

   uint64_t va = radv_buffer_get_va(image->bo);
   va += image->offset + image->clear_value_offset + base_level * 8;
   return va;
}

static inline uint64_t
radv_image_get_fce_pred_va(const struct radv_image *image, uint32_t base_level)
{
   assert(image->fce_pred_offset != 0);

   uint64_t va = radv_buffer_get_va(image->bo);
   va += image->offset + image->fce_pred_offset + base_level * 8;
   return va;
}

static inline uint64_t
radv_image_get_dcc_pred_va(const struct radv_image *image, uint32_t base_level)
{
   assert(image->dcc_pred_offset != 0);

   uint64_t va = radv_buffer_get_va(image->bo);
   va += image->offset + image->dcc_pred_offset + base_level * 8;
   return va;
}

static inline uint64_t
radv_get_tc_compat_zrange_va(const struct radv_image *image, uint32_t base_level)
{
   assert(image->tc_compat_zrange_offset != 0);

   uint64_t va = radv_buffer_get_va(image->bo);
   va += image->offset + image->tc_compat_zrange_offset + base_level * 4;
   return va;
}

static inline uint64_t
radv_get_ds_clear_value_va(const struct radv_image *image, uint32_t base_level)
{
   assert(radv_image_has_clear_value(image));

   uint64_t va = radv_buffer_get_va(image->bo);
   va += image->offset + image->clear_value_offset + base_level * 8;
   return va;
}

static inline uint32_t
radv_get_htile_initial_value(const struct radv_device *device, const struct radv_image *image)
{
   uint32_t initial_value;

   if (radv_image_tile_stencil_disabled(device, image)) {
      /* Z only (no stencil):
       *
       * |31     18|17      4|3     0|
       * +---------+---------+-------+
       * |  Max Z  |  Min Z  | ZMask |
       */
      initial_value = 0xfffc000f;
   } else {
      /* Z and stencil:
       *
       * |31       12|11 10|9    8|7   6|5   4|3     0|
       * +-----------+-----+------+-----+-----+-------+
       * |  Z Range  |     | SMem | SR1 | SR0 | ZMask |
       *
       * SR0/SR1 contains the stencil test results. Initializing
       * SR0/SR1 to 0x3 means the stencil test result is unknown.
       *
       * Z, stencil and 4 bit VRS encoding:
       * |31       12|11        10|9    8|7          6|5   4|3     0|
       * +-----------+------------+------+------------+-----+-------+
       * |  Z Range  | VRS y-rate | SMem | VRS x-rate | SR0 | ZMask |
       */
      if (radv_image_has_vrs_htile(device, image)) {
         /* Initialize the VRS x-rate value at 0, so the hw interprets it as 1 sample. */
         initial_value = 0xfffff33f;
      } else {
         initial_value = 0xfffff3ff;
      }
   }

   return initial_value;
}

static inline bool
radv_image_get_iterate256(struct radv_device *device, struct radv_image *image)
{
   /* ITERATE_256 is required for depth or stencil MSAA images that are TC-compatible HTILE. */
   return device->physical_device->rad_info.chip_class >= GFX10 &&
          (image->usage & (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT)) &&
          radv_image_is_tc_compat_htile(image) &&
          image->info.samples > 1;
}

unsigned radv_image_queue_family_mask(const struct radv_image *image, uint32_t family,
                                      uint32_t queue_family);

static inline uint32_t
radv_get_layerCount(const struct radv_image *image, const VkImageSubresourceRange *range)
{
   return range->layerCount == VK_REMAINING_ARRAY_LAYERS
             ? image->info.array_size - range->baseArrayLayer
             : range->layerCount;
}

static inline uint32_t
radv_get_levelCount(const struct radv_image *image, const VkImageSubresourceRange *range)
{
   return range->levelCount == VK_REMAINING_MIP_LEVELS ? image->info.levels - range->baseMipLevel
                                                       : range->levelCount;
}

bool radv_image_is_renderable(struct radv_device *device, struct radv_image *image);

struct radeon_bo_metadata;
void radv_init_metadata(struct radv_device *device, struct radv_image *image,
                        struct radeon_bo_metadata *metadata);

void radv_image_override_offset_stride(struct radv_device *device, struct radv_image *image,
                                       uint64_t offset, uint32_t stride);

union radv_descriptor {
   struct {
      uint32_t plane0_descriptor[8];
      uint32_t fmask_descriptor[8];
   };
   struct {
      uint32_t plane_descriptors[3][8];
   };
};

struct radv_image_view {
   struct vk_object_base base;
   struct radv_image *image; /**< VkImageViewCreateInfo::image */

   VkImageViewType type;
   VkImageAspectFlags aspect_mask;
   VkFormat vk_format;
   unsigned plane_id;
   uint32_t base_layer;
   uint32_t layer_count;
   uint32_t base_mip;
   uint32_t level_count;
   VkExtent3D extent; /**< Extent of VkImageViewCreateInfo::baseMipLevel. */

   /* Whether the image iview supports fast clear. */
   bool support_fast_clear;

   union radv_descriptor descriptor;

   /* Descriptor for use as a storage image as opposed to a sampled image.
    * This has a few differences for cube maps (e.g. type).
    */
   union radv_descriptor storage_descriptor;
};

struct radv_image_create_info {
   const VkImageCreateInfo *vk_info;
   bool scanout;
   bool no_metadata_planes;
   const struct radeon_bo_metadata *bo_metadata;
};

VkResult
radv_image_create_layout(struct radv_device *device, struct radv_image_create_info create_info,
                         const struct VkImageDrmFormatModifierExplicitCreateInfoEXT *mod_info,
                         struct radv_image *image);

VkResult radv_image_create(VkDevice _device, const struct radv_image_create_info *info,
                           const VkAllocationCallbacks *alloc, VkImage *pImage);

bool radv_are_formats_dcc_compatible(const struct radv_physical_device *pdev, const void *pNext,
                                     VkFormat format, VkImageCreateFlags flags,
                                     bool *sign_reinterpret);

bool vi_alpha_is_on_msb(struct radv_device *device, VkFormat format);

VkResult radv_image_from_gralloc(VkDevice device_h, const VkImageCreateInfo *base_info,
                                 const VkNativeBufferANDROID *gralloc_info,
                                 const VkAllocationCallbacks *alloc, VkImage *out_image_h);
uint64_t radv_ahb_usage_from_vk_usage(const VkImageCreateFlags vk_create,
                                      const VkImageUsageFlags vk_usage);
VkResult radv_import_ahb_memory(struct radv_device *device, struct radv_device_memory *mem,
                                unsigned priority,
                                const VkImportAndroidHardwareBufferInfoANDROID *info);
VkResult radv_create_ahb_memory(struct radv_device *device, struct radv_device_memory *mem,
                                unsigned priority, const VkMemoryAllocateInfo *pAllocateInfo);

VkFormat radv_select_android_external_format(const void *next, VkFormat default_format);

bool radv_android_gralloc_supports_format(VkFormat format, VkImageUsageFlagBits usage);

struct radv_image_view_extra_create_info {
   bool disable_compression;
   bool enable_compression;
};

void radv_image_view_init(struct radv_image_view *view, struct radv_device *device,
                          const VkImageViewCreateInfo *pCreateInfo,
                          const struct radv_image_view_extra_create_info *extra_create_info);

VkFormat radv_get_aspect_format(struct radv_image *image, VkImageAspectFlags mask);

struct radv_sampler_ycbcr_conversion {
   struct vk_object_base base;
   VkFormat format;
   VkSamplerYcbcrModelConversion ycbcr_model;
   VkSamplerYcbcrRange ycbcr_range;
   VkComponentMapping components;
   VkChromaLocation chroma_offsets[2];
   VkFilter chroma_filter;
};

struct radv_buffer_view {
   struct vk_object_base base;
   struct radeon_winsys_bo *bo;
   VkFormat vk_format;
   uint64_t range; /**< VkBufferViewCreateInfo::range */
   uint32_t state[4];
};
void radv_buffer_view_init(struct radv_buffer_view *view, struct radv_device *device,
                           const VkBufferViewCreateInfo *pCreateInfo);

static inline struct VkExtent3D
radv_sanitize_image_extent(const VkImageType imageType, const struct VkExtent3D imageExtent)
{
   switch (imageType) {
   case VK_IMAGE_TYPE_1D:
      return (VkExtent3D){imageExtent.width, 1, 1};
   case VK_IMAGE_TYPE_2D:
      return (VkExtent3D){imageExtent.width, imageExtent.height, 1};
   case VK_IMAGE_TYPE_3D:
      return imageExtent;
   default:
      unreachable("invalid image type");
   }
}

static inline struct VkOffset3D
radv_sanitize_image_offset(const VkImageType imageType, const struct VkOffset3D imageOffset)
{
   switch (imageType) {
   case VK_IMAGE_TYPE_1D:
      return (VkOffset3D){imageOffset.x, 0, 0};
   case VK_IMAGE_TYPE_2D:
      return (VkOffset3D){imageOffset.x, imageOffset.y, 0};
   case VK_IMAGE_TYPE_3D:
      return imageOffset;
   default:
      unreachable("invalid image type");
   }
}

static inline bool
radv_image_extent_compare(const struct radv_image *image, const VkExtent3D *extent)
{
   if (extent->width != image->info.width || extent->height != image->info.height ||
       extent->depth != image->info.depth)
      return false;
   return true;
}

struct radv_sampler {
   struct vk_object_base base;
   uint32_t state[4];
   struct radv_sampler_ycbcr_conversion *ycbcr_sampler;
   uint32_t border_color_slot;
};

struct radv_framebuffer {
   struct vk_object_base base;
   uint32_t width;
   uint32_t height;
   uint32_t layers;

   bool imageless;

   uint32_t attachment_count;
   struct radv_image_view *attachments[0];
};

struct radv_subpass_barrier {
   VkPipelineStageFlags src_stage_mask;
   VkAccessFlags src_access_mask;
   VkAccessFlags dst_access_mask;
};

void radv_subpass_barrier(struct radv_cmd_buffer *cmd_buffer,
                          const struct radv_subpass_barrier *barrier);

struct radv_subpass_attachment {
   uint32_t attachment;
   VkImageLayout layout;
   VkImageLayout stencil_layout;
   bool in_render_loop;
};

struct radv_subpass {
   uint32_t attachment_count;
   struct radv_subpass_attachment *attachments;

   uint32_t input_count;
   uint32_t color_count;
   struct radv_subpass_attachment *input_attachments;
   struct radv_subpass_attachment *color_attachments;
   struct radv_subpass_attachment *resolve_attachments;
   struct radv_subpass_attachment *depth_stencil_attachment;
   struct radv_subpass_attachment *ds_resolve_attachment;
   struct radv_subpass_attachment *vrs_attachment;
   VkResolveModeFlagBits depth_resolve_mode;
   VkResolveModeFlagBits stencil_resolve_mode;

   /** Subpass has at least one color resolve attachment */
   bool has_color_resolve;

   /** Subpass has at least one color attachment */
   bool has_color_att;

   struct radv_subpass_barrier start_barrier;

   uint32_t view_mask;

   VkSampleCountFlagBits color_sample_count;
   VkSampleCountFlagBits depth_sample_count;
   VkSampleCountFlagBits max_sample_count;

   /* Whether the subpass has ingoing/outgoing external dependencies. */
   bool has_ingoing_dep;
   bool has_outgoing_dep;
};

uint32_t radv_get_subpass_id(struct radv_cmd_buffer *cmd_buffer);

struct radv_render_pass_attachment {
   VkFormat format;
   uint32_t samples;
   VkAttachmentLoadOp load_op;
   VkAttachmentLoadOp stencil_load_op;
   VkImageLayout initial_layout;
   VkImageLayout final_layout;
   VkImageLayout stencil_initial_layout;
   VkImageLayout stencil_final_layout;

   /* The subpass id in which the attachment will be used first/last. */
   uint32_t first_subpass_idx;
   uint32_t last_subpass_idx;
};

struct radv_render_pass {
   struct vk_object_base base;
   uint32_t attachment_count;
   uint32_t subpass_count;
   struct radv_subpass_attachment *subpass_attachments;
   struct radv_render_pass_attachment *attachments;
   struct radv_subpass_barrier end_barrier;
   struct radv_subpass subpasses[0];
};

VkResult radv_device_init_meta(struct radv_device *device);
void radv_device_finish_meta(struct radv_device *device);

struct radv_query_pool {
   struct vk_object_base base;
   struct radeon_winsys_bo *bo;
   uint32_t stride;
   uint32_t availability_offset;
   uint64_t size;
   char *ptr;
   VkQueryType type;
   uint32_t pipeline_stats_mask;
};

typedef enum {
   RADV_SEMAPHORE_NONE,
   RADV_SEMAPHORE_SYNCOBJ,
   RADV_SEMAPHORE_TIMELINE_SYNCOBJ,
   RADV_SEMAPHORE_TIMELINE,
} radv_semaphore_kind;

struct radv_deferred_queue_submission;

struct radv_timeline_waiter {
   struct list_head list;
   struct radv_deferred_queue_submission *submission;
   uint64_t value;
};

struct radv_timeline_point {
   struct list_head list;

   uint64_t value;
   uint32_t syncobj;

   /* Separate from the list to accomodate CPU wait being async, as well
    * as prevent point deletion during submission. */
   unsigned wait_count;
};

struct radv_timeline {
   mtx_t mutex;

   uint64_t highest_signaled;
   uint64_t highest_submitted;

   struct list_head points;

   /* Keep free points on hand so we do not have to recreate syncobjs all
    * the time. */
   struct list_head free_points;

   /* Submissions that are deferred waiting for a specific value to be
    * submitted. */
   struct list_head waiters;
};

struct radv_timeline_syncobj {
   /* Keep syncobj first, so common-code can just handle this as
    * non-timeline syncobj. */
   uint32_t syncobj;
   uint64_t max_point; /* max submitted point. */
};

struct radv_semaphore_part {
   radv_semaphore_kind kind;
   union {
      uint32_t syncobj;
      struct radv_timeline timeline;
      struct radv_timeline_syncobj timeline_syncobj;
   };
};

struct radv_semaphore {
   struct vk_object_base base;
   struct radv_semaphore_part permanent;
   struct radv_semaphore_part temporary;
};

bool radv_queue_internal_submit(struct radv_queue *queue, struct radeon_cmdbuf *cs);

void radv_set_descriptor_set(struct radv_cmd_buffer *cmd_buffer, VkPipelineBindPoint bind_point,
                             struct radv_descriptor_set *set, unsigned idx);

void radv_update_descriptor_sets(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                                 VkDescriptorSet overrideSet, uint32_t descriptorWriteCount,
                                 const VkWriteDescriptorSet *pDescriptorWrites,
                                 uint32_t descriptorCopyCount,
                                 const VkCopyDescriptorSet *pDescriptorCopies);

void radv_update_descriptor_set_with_template(struct radv_device *device,
                                              struct radv_cmd_buffer *cmd_buffer,
                                              struct radv_descriptor_set *set,
                                              VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                              const void *pData);

void radv_meta_push_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
                                   VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout _layout,
                                   uint32_t set, uint32_t descriptorWriteCount,
                                   const VkWriteDescriptorSet *pDescriptorWrites);

uint32_t radv_init_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                       const VkImageSubresourceRange *range, uint32_t value);

uint32_t radv_init_fmask(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                         const VkImageSubresourceRange *range);

typedef enum {
   RADV_FENCE_NONE,
   RADV_FENCE_SYNCOBJ,
} radv_fence_kind;

struct radv_fence_part {
   radv_fence_kind kind;

   /* DRM syncobj handle for syncobj-based fences. */
   uint32_t syncobj;
};

struct radv_fence {
   struct vk_object_base base;
   struct radv_fence_part permanent;
   struct radv_fence_part temporary;
};

/* radv_nir_to_llvm.c */
struct radv_shader_args;

void llvm_compile_shader(struct radv_device *device, unsigned shader_count,
                         struct nir_shader *const *shaders, struct radv_shader_binary **binary,
                         struct radv_shader_args *args);

/* radv_shader_info.h */
struct radv_shader_info;
struct radv_shader_variant_key;

void radv_nir_shader_info_pass(struct radv_device *device, const struct nir_shader *nir,
                               const struct radv_pipeline_layout *layout,
                               const struct radv_shader_variant_key *key,
                               struct radv_shader_info *info);

void radv_nir_shader_info_init(struct radv_shader_info *info);

bool radv_thread_trace_init(struct radv_device *device);
void radv_thread_trace_finish(struct radv_device *device);
bool radv_begin_thread_trace(struct radv_queue *queue);
bool radv_end_thread_trace(struct radv_queue *queue);
bool radv_get_thread_trace(struct radv_queue *queue, struct ac_thread_trace *thread_trace);
void radv_emit_thread_trace_userdata(const struct radv_device *device, struct radeon_cmdbuf *cs,
                                     const void *data, uint32_t num_dwords);
bool radv_is_instruction_timing_enabled(void);

/* radv_sqtt_layer_.c */
struct radv_barrier_data {
   union {
      struct {
         uint16_t depth_stencil_expand : 1;
         uint16_t htile_hiz_range_expand : 1;
         uint16_t depth_stencil_resummarize : 1;
         uint16_t dcc_decompress : 1;
         uint16_t fmask_decompress : 1;
         uint16_t fast_clear_eliminate : 1;
         uint16_t fmask_color_expand : 1;
         uint16_t init_mask_ram : 1;
         uint16_t reserved : 8;
      };
      uint16_t all;
   } layout_transitions;
};

/**
 * Value for the reason field of an RGP barrier start marker originating from
 * the Vulkan client (does not include PAL-defined values). (Table 15)
 */
enum rgp_barrier_reason {
   RGP_BARRIER_UNKNOWN_REASON = 0xFFFFFFFF,

   /* External app-generated barrier reasons, i.e. API synchronization
    * commands Range of valid values: [0x00000001 ... 0x7FFFFFFF].
    */
   RGP_BARRIER_EXTERNAL_CMD_PIPELINE_BARRIER = 0x00000001,
   RGP_BARRIER_EXTERNAL_RENDER_PASS_SYNC = 0x00000002,
   RGP_BARRIER_EXTERNAL_CMD_WAIT_EVENTS = 0x00000003,

   /* Internal barrier reasons, i.e. implicit synchronization inserted by
    * the Vulkan driver Range of valid values: [0xC0000000 ... 0xFFFFFFFE].
    */
   RGP_BARRIER_INTERNAL_BASE = 0xC0000000,
   RGP_BARRIER_INTERNAL_PRE_RESET_QUERY_POOL_SYNC = RGP_BARRIER_INTERNAL_BASE + 0,
   RGP_BARRIER_INTERNAL_POST_RESET_QUERY_POOL_SYNC = RGP_BARRIER_INTERNAL_BASE + 1,
   RGP_BARRIER_INTERNAL_GPU_EVENT_RECYCLE_STALL = RGP_BARRIER_INTERNAL_BASE + 2,
   RGP_BARRIER_INTERNAL_PRE_COPY_QUERY_POOL_RESULTS_SYNC = RGP_BARRIER_INTERNAL_BASE + 3
};

void radv_describe_begin_cmd_buffer(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_end_cmd_buffer(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_draw(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_dispatch(struct radv_cmd_buffer *cmd_buffer, int x, int y, int z);
void radv_describe_begin_render_pass_clear(struct radv_cmd_buffer *cmd_buffer,
                                           VkImageAspectFlagBits aspects);
void radv_describe_end_render_pass_clear(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_begin_render_pass_resolve(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_end_render_pass_resolve(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_barrier_start(struct radv_cmd_buffer *cmd_buffer,
                                 enum rgp_barrier_reason reason);
void radv_describe_barrier_end(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_barrier_end_delayed(struct radv_cmd_buffer *cmd_buffer);
void radv_describe_layout_transition(struct radv_cmd_buffer *cmd_buffer,
                                     const struct radv_barrier_data *barrier);

uint64_t radv_get_current_time(void);

static inline uint32_t
si_conv_gl_prim_to_vertices(unsigned gl_prim)
{
   switch (gl_prim) {
   case 0: /* GL_POINTS */
      return 1;
   case 1: /* GL_LINES */
   case 3: /* GL_LINE_STRIP */
      return 2;
   case 4: /* GL_TRIANGLES */
   case 5: /* GL_TRIANGLE_STRIP */
      return 3;
   case 0xA: /* GL_LINE_STRIP_ADJACENCY_ARB */
      return 4;
   case 0xc: /* GL_TRIANGLES_ADJACENCY_ARB */
      return 6;
   case 7: /* GL_QUADS */
      return V_028A6C_TRISTRIP;
   default:
      assert(0);
      return 0;
   }
}

struct radv_extra_render_pass_begin_info {
   bool disable_dcc;
};

void radv_cmd_buffer_begin_render_pass(struct radv_cmd_buffer *cmd_buffer,
                                       const VkRenderPassBeginInfo *pRenderPassBegin,
                                       const struct radv_extra_render_pass_begin_info *extra_info);
void radv_cmd_buffer_end_render_pass(struct radv_cmd_buffer *cmd_buffer);

static inline uint32_t
si_translate_prim(unsigned topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return V_008958_DI_PT_POINTLIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return V_008958_DI_PT_LINELIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return V_008958_DI_PT_LINESTRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return V_008958_DI_PT_TRILIST;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return V_008958_DI_PT_TRISTRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return V_008958_DI_PT_TRIFAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      return V_008958_DI_PT_LINELIST_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return V_008958_DI_PT_LINESTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      return V_008958_DI_PT_TRILIST_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return V_008958_DI_PT_TRISTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return V_008958_DI_PT_PATCH;
   default:
      assert(0);
      return 0;
   }
}

static inline uint32_t
si_translate_stencil_op(enum VkStencilOp op)
{
   switch (op) {
   case VK_STENCIL_OP_KEEP:
      return V_02842C_STENCIL_KEEP;
   case VK_STENCIL_OP_ZERO:
      return V_02842C_STENCIL_ZERO;
   case VK_STENCIL_OP_REPLACE:
      return V_02842C_STENCIL_REPLACE_TEST;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
      return V_02842C_STENCIL_ADD_CLAMP;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
      return V_02842C_STENCIL_SUB_CLAMP;
   case VK_STENCIL_OP_INVERT:
      return V_02842C_STENCIL_INVERT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:
      return V_02842C_STENCIL_ADD_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:
      return V_02842C_STENCIL_SUB_WRAP;
   default:
      return 0;
   }
}

static inline uint32_t
si_translate_blend_logic_op(VkLogicOp op)
{
   switch (op) {
   case VK_LOGIC_OP_CLEAR:
      return V_028808_ROP3_CLEAR;
   case VK_LOGIC_OP_AND:
      return V_028808_ROP3_AND;
   case VK_LOGIC_OP_AND_REVERSE:
      return V_028808_ROP3_AND_REVERSE;
   case VK_LOGIC_OP_COPY:
      return V_028808_ROP3_COPY;
   case VK_LOGIC_OP_AND_INVERTED:
      return V_028808_ROP3_AND_INVERTED;
   case VK_LOGIC_OP_NO_OP:
      return V_028808_ROP3_NO_OP;
   case VK_LOGIC_OP_XOR:
      return V_028808_ROP3_XOR;
   case VK_LOGIC_OP_OR:
      return V_028808_ROP3_OR;
   case VK_LOGIC_OP_NOR:
      return V_028808_ROP3_NOR;
   case VK_LOGIC_OP_EQUIVALENT:
      return V_028808_ROP3_EQUIVALENT;
   case VK_LOGIC_OP_INVERT:
      return V_028808_ROP3_INVERT;
   case VK_LOGIC_OP_OR_REVERSE:
      return V_028808_ROP3_OR_REVERSE;
   case VK_LOGIC_OP_COPY_INVERTED:
      return V_028808_ROP3_COPY_INVERTED;
   case VK_LOGIC_OP_OR_INVERTED:
      return V_028808_ROP3_OR_INVERTED;
   case VK_LOGIC_OP_NAND:
      return V_028808_ROP3_NAND;
   case VK_LOGIC_OP_SET:
      return V_028808_ROP3_SET;
   default:
      unreachable("Unhandled logic op");
   }
}

/**
 * Helper used for debugging compiler issues by enabling/disabling LLVM for a
 * specific shader stage (developers only).
 */
static inline bool
radv_use_llvm_for_stage(struct radv_device *device, UNUSED gl_shader_stage stage)
{
   return device->physical_device->use_llvm;
}

struct radv_acceleration_structure {
   struct vk_object_base base;

   struct radeon_winsys_bo *bo;
   uint64_t mem_offset;
   uint64_t size;
};

static inline uint64_t
radv_accel_struct_get_va(const struct radv_acceleration_structure *accel)
{
   return radv_buffer_get_va(accel->bo) + accel->mem_offset;
}

#define RADV_DEFINE_HANDLE_CASTS(__radv_type, __VkType)                                            \
                                                                                                   \
   static inline struct __radv_type *__radv_type##_from_handle(__VkType _handle)                   \
   {                                                                                               \
      return (struct __radv_type *)_handle;                                                        \
   }                                                                                               \
                                                                                                   \
   static inline __VkType __radv_type##_to_handle(struct __radv_type *_obj)                        \
   {                                                                                               \
      return (__VkType)_obj;                                                                       \
   }

#define RADV_DEFINE_NONDISP_HANDLE_CASTS(__radv_type, __VkType)                                    \
                                                                                                   \
   static inline struct __radv_type *__radv_type##_from_handle(__VkType _handle)                   \
   {                                                                                               \
      return (struct __radv_type *)(uintptr_t)_handle;                                             \
   }                                                                                               \
                                                                                                   \
   static inline __VkType __radv_type##_to_handle(struct __radv_type *_obj)                        \
   {                                                                                               \
      return (__VkType)(uintptr_t)_obj;                                                            \
   }

#define RADV_FROM_HANDLE(__radv_type, __name, __handle)                                            \
   struct __radv_type *__name = __radv_type##_from_handle(__handle)

RADV_DEFINE_HANDLE_CASTS(radv_cmd_buffer, VkCommandBuffer)
RADV_DEFINE_HANDLE_CASTS(radv_device, VkDevice)
RADV_DEFINE_HANDLE_CASTS(radv_instance, VkInstance)
RADV_DEFINE_HANDLE_CASTS(radv_physical_device, VkPhysicalDevice)
RADV_DEFINE_HANDLE_CASTS(radv_queue, VkQueue)

RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_acceleration_structure, VkAccelerationStructureKHR)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_cmd_pool, VkCommandPool)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_buffer, VkBuffer)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_buffer_view, VkBufferView)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_pool, VkDescriptorPool)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_set, VkDescriptorSet)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_set_layout, VkDescriptorSetLayout)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_update_template, VkDescriptorUpdateTemplate)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_device_memory, VkDeviceMemory)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_fence, VkFence)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_event, VkEvent)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_framebuffer, VkFramebuffer)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_image, VkImage)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_image_view, VkImageView);
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline_cache, VkPipelineCache)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline, VkPipeline)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline_layout, VkPipelineLayout)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_query_pool, VkQueryPool)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_render_pass, VkRenderPass)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_sampler, VkSampler)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_sampler_ycbcr_conversion, VkSamplerYcbcrConversion)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_semaphore, VkSemaphore)

#endif /* RADV_PRIVATE_H */
