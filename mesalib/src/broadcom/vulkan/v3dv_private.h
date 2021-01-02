/*
 * Copyright © 2019 Raspberry Pi
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
#ifndef V3DV_PRIVATE_H
#define V3DV_PRIVATE_H

#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include <vk_enum_to_str.h>

#include "vk_object.h"

#include <xf86drm.h>

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x) ((void)0)
#endif

#include "v3dv_limits.h"

#include "common/v3d_device_info.h"
#include "common/v3d_limits.h"

#include "compiler/shader_enums.h"
#include "compiler/spirv/nir_spirv.h"

#include "compiler/v3d_compiler.h"

#include "vk_debug_report.h"
#include "util/set.h"
#include "util/hash_table.h"
#include "util/xmlconfig.h"
#include "u_atomic.h"

#include "v3dv_entrypoints.h"
#include "v3dv_extensions.h"
#include "v3dv_bo.h"

#include "drm-uapi/v3d_drm.h"

/* FIXME: hooks for the packet definition functions. */
static inline void
pack_emit_reloc(void *cl, const void *reloc) {}

#define __gen_user_data struct v3dv_cl
#define __gen_address_type struct v3dv_cl_reloc
#define __gen_address_offset(reloc) (((reloc)->bo ? (reloc)->bo->offset : 0) + \
                                     (reloc)->offset)
#define __gen_emit_reloc cl_pack_emit_reloc
#define __gen_unpack_address(cl, s, e) __unpack_address(cl, s, e)
#include "v3dv_cl.h"

#include "vk_alloc.h"
#include "simulator/v3d_simulator.h"


/* FIXME: pipe_box from Gallium. Needed for some v3d_tiling.c functions.
 * In the future we might want to drop that depedency, but for now it is
 * good enough.
 */
#include "util/u_box.h"
#include "wsi_common.h"

#include "broadcom/cle/v3dx_pack.h"

/* A non-fatal assert.  Useful for debugging. */
#ifdef DEBUG
#define v3dv_assert(x) ({ \
   if (unlikely(!(x))) \
      fprintf(stderr, "%s:%d ASSERT: %s", __FILE__, __LINE__, #x); \
})
#else
#define v3dv_assert(x)
#endif

#define perf_debug(...) do {                       \
   if (unlikely(V3D_DEBUG & V3D_DEBUG_PERF))       \
      fprintf(stderr, __VA_ARGS__);                \
} while (0)

#define for_each_bit(b, dword)                                               \
   for (uint32_t __dword = (dword);                                          \
        (b) = __builtin_ffs(__dword) - 1, __dword; __dword &= ~(1 << (b)))

struct v3dv_instance;

#ifdef USE_V3D_SIMULATOR
#define using_v3d_simulator true
#else
#define using_v3d_simulator false
#endif

struct v3d_simulator_file;

struct v3dv_physical_device {
   struct vk_object_base base;

   struct v3dv_instance *instance;

   struct v3dv_device_extension_table supported_extensions;
   struct v3dv_physical_device_dispatch_table dispatch;

   char *name;
   int32_t render_fd;
   int32_t display_fd;
   int32_t master_fd;

   uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];

   mtx_t mutex;

   struct wsi_device wsi_device;

   VkPhysicalDeviceMemoryProperties memory;

   struct v3d_device_info devinfo;

   struct v3d_simulator_file *sim_file;

   const struct v3d_compiler *compiler;
   uint32_t next_program_id;

   struct {
      bool merge_jobs;
   } options;
};

VkResult v3dv_physical_device_acquire_display(struct v3dv_instance *instance,
                                              struct v3dv_physical_device *pdevice,
                                              VkIcdSurfaceBase *surface);

VkResult v3dv_wsi_init(struct v3dv_physical_device *physical_device);
void v3dv_wsi_finish(struct v3dv_physical_device *physical_device);

void v3dv_meta_clear_init(struct v3dv_device *device);
void v3dv_meta_clear_finish(struct v3dv_device *device);

void v3dv_meta_blit_init(struct v3dv_device *device);
void v3dv_meta_blit_finish(struct v3dv_device *device);

void v3dv_meta_texel_buffer_copy_init(struct v3dv_device *device);
void v3dv_meta_texel_buffer_copy_finish(struct v3dv_device *device);

struct v3dv_app_info {
   const char *app_name;
   uint32_t app_version;
   const char *engine_name;
   uint32_t engine_version;
   uint32_t api_version;
};

struct v3dv_instance {
   struct vk_object_base base;

   VkAllocationCallbacks alloc;

   struct v3dv_app_info app_info;

   struct v3dv_instance_extension_table enabled_extensions;
   struct v3dv_instance_dispatch_table dispatch;
   struct v3dv_device_dispatch_table device_dispatch;

   int physicalDeviceCount;
   struct v3dv_physical_device physicalDevice;

   struct vk_debug_report_instance debug_report_callbacks;

   bool pipeline_cache_enabled;
   bool default_pipeline_cache_enabled;
};

/* Tracks wait threads spawned from a single vkQueueSubmit call */
struct v3dv_queue_submit_wait_info {
   /*  struct vk_object_base base; ?*/
   struct list_head list_link;

   struct v3dv_device *device;

   /* List of wait threads spawned for any command buffers in a particular
    * call to vkQueueSubmit.
    */
   uint32_t wait_thread_count;
   struct {
      pthread_t thread;
      bool finished;
   } wait_threads[16];

   /* The master wait thread for the entire submit. This will wait for all
    * other threads in this submit to complete  before processing signal
    * semaphores and fences.
    */
   pthread_t master_wait_thread;

   /* List of semaphores (and fence) to signal after all wait threads completed
    * and all command buffer jobs in the submission have been sent to the GPU.
    */
   uint32_t signal_semaphore_count;
   VkSemaphore *signal_semaphores;
   VkFence fence;
};

struct v3dv_queue {
   struct vk_object_base base;

   struct v3dv_device *device;
   VkDeviceQueueCreateFlags flags;

   /* A list of active v3dv_queue_submit_wait_info */
   struct list_head submit_wait_list;

   /* A mutex to prevent concurrent access to the list of wait threads */
   mtx_t mutex;

   struct v3dv_job *noop_job;
};

#define V3DV_META_BLIT_CACHE_KEY_SIZE              (4 * sizeof(uint32_t))
#define V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE (1 * sizeof(uint32_t))

struct v3dv_meta_color_clear_pipeline {
   VkPipeline pipeline;
   VkRenderPass pass;
   bool cached;
   uint64_t key;
};

struct v3dv_meta_depth_clear_pipeline {
   VkPipeline pipeline;
   uint64_t key;
};

struct v3dv_meta_blit_pipeline {
   VkPipeline pipeline;
   VkRenderPass pass;
   VkRenderPass pass_no_load;
   uint8_t key[V3DV_META_BLIT_CACHE_KEY_SIZE];
};

struct v3dv_meta_texel_buffer_copy_pipeline {
   VkPipeline pipeline;
   VkRenderPass pass;
   VkRenderPass pass_no_load;
   uint8_t key[V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE];
};

struct v3dv_pipeline_cache_stats {
   uint32_t miss;
   uint32_t hit;
   uint32_t count;
};

struct v3dv_pipeline_cache {
   struct vk_object_base base;

   struct v3dv_device *device;
   mtx_t mutex;

   struct hash_table *nir_cache;
   struct v3dv_pipeline_cache_stats nir_stats;

   struct hash_table *variant_cache;
   struct v3dv_pipeline_cache_stats variant_stats;
};

struct v3dv_device {
   struct vk_device vk;

   struct v3dv_instance *instance;
   struct v3dv_physical_device *pdevice;

   struct v3dv_device_extension_table enabled_extensions;
   struct v3dv_device_dispatch_table dispatch;

   struct v3d_device_info devinfo;
   struct v3dv_queue queue;

   /* A sync object to track the last job submitted to the GPU. */
   uint32_t last_job_sync;

   /* A mutex to prevent concurrent access to last_job_sync from the queue */
   mtx_t mutex;

   /* Resources used for meta operations */
   struct {
      mtx_t mtx;
      struct {
         VkPipelineLayout p_layout;
         struct hash_table *cache; /* v3dv_meta_color_clear_pipeline */
      } color_clear;
      struct {
         VkPipelineLayout p_layout;
         struct hash_table *cache; /* v3dv_meta_depth_clear_pipeline */
      } depth_clear;
      struct {
         VkDescriptorSetLayout ds_layout;
         VkPipelineLayout p_layout;
         struct hash_table *cache[3]; /* v3dv_meta_blit_pipeline for 1d, 2d, 3d */
      } blit;
      struct {
         VkDescriptorSetLayout ds_layout;
         VkPipelineLayout p_layout;
         struct hash_table *cache[3]; /* v3dv_meta_texel_buffer_copy_pipeline for 1d, 2d, 3d */
      } texel_buffer_copy;
   } meta;

   struct v3dv_bo_cache {
      /** List of struct v3d_bo freed, by age. */
      struct list_head time_list;
      /** List of struct v3d_bo freed, per size, by age. */
      struct list_head *size_list;
      uint32_t size_list_size;

      mtx_t lock;

      uint32_t cache_size;
      uint32_t cache_count;
      uint32_t max_cache_size;
   } bo_cache;

   uint32_t bo_size;
   uint32_t bo_count;

   struct v3dv_pipeline_cache default_pipeline_cache;

   VkPhysicalDeviceFeatures features;
};

struct v3dv_device_memory {
   struct vk_object_base base;

   struct v3dv_bo *bo;
   const VkMemoryType *type;
   bool has_bo_ownership;
   bool is_for_wsi;
};

#define V3D_OUTPUT_IMAGE_FORMAT_NO 255
#define TEXTURE_DATA_FORMAT_NO     255

struct v3dv_format {
   bool supported;

   /* One of V3D33_OUTPUT_IMAGE_FORMAT_*, or OUTPUT_IMAGE_FORMAT_NO */
   uint8_t rt_type;

   /* One of V3D33_TEXTURE_DATA_FORMAT_*. */
   uint8_t tex_type;

   /* Swizzle to apply to the RGBA shader output for storing to the tile
    * buffer, to the RGBA tile buffer to produce shader input (for
    * blending), and for turning the rgba8888 texture sampler return
    * value into shader rgba values.
    */
   uint8_t swizzle[4];

   /* Whether the return value is 16F/I/UI or 32F/I/UI. */
   uint8_t return_size;

   /* If the format supports (linear) filtering when texturing. */
   bool supports_filtering;
};

/**
 * Tiling mode enum used for v3d_resource.c, which maps directly to the Memory
 * Format field of render target and Z/Stencil config.
 */
enum v3d_tiling_mode {
   /* Untiled resources.  Not valid as texture inputs. */
   VC5_TILING_RASTER,

   /* Single line of u-tiles. */
   VC5_TILING_LINEARTILE,

   /* Departure from standard 4-UIF block column format. */
   VC5_TILING_UBLINEAR_1_COLUMN,

   /* Departure from standard 4-UIF block column format. */
   VC5_TILING_UBLINEAR_2_COLUMN,

   /* Normal tiling format: grouped in 4x4 UIFblocks, each of which is
    * split 2x2 into utiles.
    */
   VC5_TILING_UIF_NO_XOR,

   /* Normal tiling format: grouped in 4x4 UIFblocks, each of which is
    * split 2x2 into utiles.
    */
   VC5_TILING_UIF_XOR,
};

struct v3d_resource_slice {
   uint32_t offset;
   uint32_t stride;
   uint32_t padded_height;
   /* Size of a single pane of the slice.  For 3D textures, there will be
    * a number of panes equal to the minified, power-of-two-aligned
    * depth.
    */
   uint32_t size;
   uint8_t ub_pad;
   enum v3d_tiling_mode tiling;
   uint32_t padded_height_of_output_image_in_uif_blocks;
};

struct v3dv_image {
   struct vk_object_base base;

   VkImageType type;
   VkImageAspectFlags aspects;

   VkExtent3D extent;
   uint32_t levels;
   uint32_t array_size;
   uint32_t samples;
   VkImageUsageFlags usage;
   VkImageCreateFlags flags;
   VkImageTiling tiling;

   VkFormat vk_format;
   const struct v3dv_format *format;

   uint32_t cpp;

   uint64_t drm_format_mod;
   bool tiled;

   struct v3d_resource_slice slices[V3D_MAX_MIP_LEVELS];
   uint64_t size; /* Total size in bytes */
   uint32_t cube_map_stride;
   uint32_t alignment;

   struct v3dv_device_memory *mem;
   VkDeviceSize mem_offset;
};

VkImageViewType v3dv_image_type_to_view_type(VkImageType type);

struct v3dv_image_view {
   struct vk_object_base base;

   const struct v3dv_image *image;
   VkImageAspectFlags aspects;
   VkExtent3D extent;
   VkImageViewType type;

   VkFormat vk_format;
   const struct v3dv_format *format;
   bool swap_rb;
   uint32_t internal_bpp;
   uint32_t internal_type;

   uint32_t base_level;
   uint32_t max_level;
   uint32_t first_layer;
   uint32_t last_layer;
   uint32_t offset;

   /* Precomputed (composed from createinfo->components and formar swizzle)
    * swizzles to pass in to the shader key.
    *
    * This could be also included on the descriptor bo, but the shader state
    * packet doesn't need it on a bo, so we can just avoid a memory copy
    */
   uint8_t swizzle[4];

   /* Prepacked TEXTURE_SHADER_STATE. It will be copied to the descriptor info
    * during UpdateDescriptorSets.
    *
    * Empirical tests show that cube arrays need a different shader state
    * depending on whether they are used with a sampler or not, so for these
    * we generate two states and select the one to use based on the descriptor
    * type.
    */
   uint8_t texture_shader_state[2][cl_packet_length(TEXTURE_SHADER_STATE)];
};

uint32_t v3dv_layer_offset(const struct v3dv_image *image, uint32_t level, uint32_t layer);

struct v3dv_buffer {
   struct vk_object_base base;

   VkDeviceSize size;
   VkBufferUsageFlags usage;
   uint32_t alignment;

   struct v3dv_device_memory *mem;
   VkDeviceSize mem_offset;
};

struct v3dv_buffer_view {
   struct vk_object_base base;

   const struct v3dv_buffer *buffer;

   VkFormat vk_format;
   const struct v3dv_format *format;
   uint32_t internal_bpp;
   uint32_t internal_type;

   uint32_t offset;
   uint32_t size;
   uint32_t num_elements;

   /* Prepacked TEXTURE_SHADER_STATE. */
   uint8_t texture_shader_state[cl_packet_length(TEXTURE_SHADER_STATE)];
};

struct v3dv_subpass_attachment {
   uint32_t attachment;
   VkImageLayout layout;
};

struct v3dv_subpass {
   uint32_t input_count;
   struct v3dv_subpass_attachment *input_attachments;

   uint32_t color_count;
   struct v3dv_subpass_attachment *color_attachments;
   struct v3dv_subpass_attachment *resolve_attachments;

   struct v3dv_subpass_attachment ds_attachment;

   bool has_srgb_rt;

   /* If we need to emit the clear of the depth/stencil attachment using a
    * a draw call instead of using the TLB (GFXH-1461).
    */
   bool do_depth_clear_with_draw;
   bool do_stencil_clear_with_draw;
};

struct v3dv_render_pass_attachment {
   VkAttachmentDescription desc;
   uint32_t first_subpass;
   uint32_t last_subpass;

   /* If this is a multismapled attachment that is going to be resolved,
    * whether we can use the TLB resolve on store.
    */
   bool use_tlb_resolve;
};

struct v3dv_render_pass {
   struct vk_object_base base;

   uint32_t attachment_count;
   struct v3dv_render_pass_attachment *attachments;

   uint32_t subpass_count;
   struct v3dv_subpass *subpasses;

   struct v3dv_subpass_attachment *subpass_attachments;
};

struct v3dv_framebuffer {
   struct vk_object_base base;

   uint32_t width;
   uint32_t height;
   uint32_t layers;

   /* Typically, edge tiles in the framebuffer have padding depending on the
    * underlying tiling layout. One consequnce of this is that when the
    * framebuffer dimensions are not aligned to tile boundaries, tile stores
    * would still write full tiles on the edges and write to the padded area.
    * If the framebuffer is aliasing a smaller region of a larger image, then
    * we need to be careful with this though, as we won't have padding on the
    * edge tiles (which typically means that we need to load the tile buffer
    * before we store).
    */
   bool has_edge_padding;

   uint32_t attachment_count;
   uint32_t color_attachment_count;
   struct v3dv_image_view *attachments[0];
};

struct v3dv_frame_tiling {
   uint32_t width;
   uint32_t height;
   uint32_t layers;
   uint32_t render_target_count;
   uint32_t internal_bpp;
   bool     msaa;
   uint32_t tile_width;
   uint32_t tile_height;
   uint32_t draw_tiles_x;
   uint32_t draw_tiles_y;
   uint32_t supertile_width;
   uint32_t supertile_height;
   uint32_t frame_width_in_supertiles;
   uint32_t frame_height_in_supertiles;
};

void v3dv_framebuffer_compute_internal_bpp_msaa(const struct v3dv_framebuffer *framebuffer,
                                                const struct v3dv_subpass *subpass,
                                                uint8_t *max_bpp, bool *msaa);

bool v3dv_subpass_area_is_tile_aligned(const VkRect2D *area,
                                       struct v3dv_framebuffer *fb,
                                       struct v3dv_render_pass *pass,
                                       uint32_t subpass_idx);
struct v3dv_cmd_pool {
   struct vk_object_base base;

   VkAllocationCallbacks alloc;
   struct list_head cmd_buffers;
};

enum v3dv_cmd_buffer_status {
   V3DV_CMD_BUFFER_STATUS_NEW           = 0,
   V3DV_CMD_BUFFER_STATUS_INITIALIZED   = 1,
   V3DV_CMD_BUFFER_STATUS_RECORDING     = 2,
   V3DV_CMD_BUFFER_STATUS_EXECUTABLE    = 3
};

union v3dv_clear_value {
   uint32_t color[4];
   struct {
      float z;
      uint8_t s;
   };
};

struct v3dv_cmd_buffer_attachment_state {
   /* The original clear value as provided by the Vulkan API */
   VkClearValue vk_clear_value;

   /* The hardware clear value */
   union v3dv_clear_value clear_value;
};

void v3dv_get_hw_clear_color(const VkClearColorValue *color,
                             uint32_t internal_type,
                             uint32_t internal_size,
                             uint32_t *hw_color);

struct v3dv_viewport_state {
   uint32_t count;
   VkViewport viewports[MAX_VIEWPORTS];
   float translate[MAX_VIEWPORTS][3];
   float scale[MAX_VIEWPORTS][3];
};

struct v3dv_scissor_state {
   uint32_t count;
   VkRect2D scissors[MAX_SCISSORS];
};

/* Mostly a v3dv mapping of VkDynamicState, used to track which data as
 * defined as dynamic
 */
enum v3dv_dynamic_state_bits {
   V3DV_DYNAMIC_VIEWPORT                  = 1 << 0,
   V3DV_DYNAMIC_SCISSOR                   = 1 << 1,
   V3DV_DYNAMIC_STENCIL_COMPARE_MASK      = 1 << 2,
   V3DV_DYNAMIC_STENCIL_WRITE_MASK        = 1 << 3,
   V3DV_DYNAMIC_STENCIL_REFERENCE         = 1 << 4,
   V3DV_DYNAMIC_BLEND_CONSTANTS           = 1 << 5,
   V3DV_DYNAMIC_DEPTH_BIAS                = 1 << 6,
   V3DV_DYNAMIC_LINE_WIDTH                = 1 << 7,
   V3DV_DYNAMIC_ALL                       = (1 << 8) - 1,
};

/* Flags for dirty pipeline state.
 */
enum v3dv_cmd_dirty_bits {
   V3DV_CMD_DIRTY_VIEWPORT                  = 1 << 0,
   V3DV_CMD_DIRTY_SCISSOR                   = 1 << 1,
   V3DV_CMD_DIRTY_STENCIL_COMPARE_MASK      = 1 << 2,
   V3DV_CMD_DIRTY_STENCIL_WRITE_MASK        = 1 << 3,
   V3DV_CMD_DIRTY_STENCIL_REFERENCE         = 1 << 4,
   V3DV_CMD_DIRTY_PIPELINE                  = 1 << 5,
   V3DV_CMD_DIRTY_VERTEX_BUFFER             = 1 << 6,
   V3DV_CMD_DIRTY_INDEX_BUFFER              = 1 << 7,
   V3DV_CMD_DIRTY_DESCRIPTOR_SETS           = 1 << 8,
   V3DV_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS   = 1 << 9,
   V3DV_CMD_DIRTY_PUSH_CONSTANTS            = 1 << 10,
   V3DV_CMD_DIRTY_BLEND_CONSTANTS           = 1 << 11,
   V3DV_CMD_DIRTY_OCCLUSION_QUERY           = 1 << 12,
   V3DV_CMD_DIRTY_DEPTH_BIAS                = 1 << 13,
   V3DV_CMD_DIRTY_LINE_WIDTH                = 1 << 14,
};

struct v3dv_dynamic_state {
   /**
    * Bitmask of (1 << VK_DYNAMIC_STATE_*).
    * Defines the set of saved dynamic state.
    */
   uint32_t mask;

   struct v3dv_viewport_state viewport;

   struct v3dv_scissor_state scissor;

   struct {
      uint32_t front;
      uint32_t back;
   } stencil_compare_mask;

   struct {
      uint32_t front;
      uint32_t back;
   } stencil_write_mask;

   struct {
      uint32_t front;
      uint32_t back;
   } stencil_reference;

   float blend_constants[4];

   struct {
      float constant_factor;
      float slope_factor;
   } depth_bias;

   float line_width;
};

extern const struct v3dv_dynamic_state default_dynamic_state;

void v3dv_viewport_compute_xform(const VkViewport *viewport,
                                 float scale[3],
                                 float translate[3]);

enum v3dv_ez_state {
   VC5_EZ_UNDECIDED = 0,
   VC5_EZ_GT_GE,
   VC5_EZ_LT_LE,
   VC5_EZ_DISABLED,
};

enum v3dv_job_type {
   V3DV_JOB_TYPE_GPU_CL = 0,
   V3DV_JOB_TYPE_GPU_CL_SECONDARY,
   V3DV_JOB_TYPE_GPU_TFU,
   V3DV_JOB_TYPE_GPU_CSD,
   V3DV_JOB_TYPE_CPU_RESET_QUERIES,
   V3DV_JOB_TYPE_CPU_END_QUERY,
   V3DV_JOB_TYPE_CPU_COPY_QUERY_RESULTS,
   V3DV_JOB_TYPE_CPU_SET_EVENT,
   V3DV_JOB_TYPE_CPU_WAIT_EVENTS,
   V3DV_JOB_TYPE_CPU_CLEAR_ATTACHMENTS,
   V3DV_JOB_TYPE_CPU_COPY_BUFFER_TO_IMAGE,
   V3DV_JOB_TYPE_CPU_CSD_INDIRECT,
   V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY,
};

struct v3dv_reset_query_cpu_job_info {
   struct v3dv_query_pool *pool;
   uint32_t first;
   uint32_t count;
};

struct v3dv_end_query_cpu_job_info {
   struct v3dv_query_pool *pool;
   uint32_t query;
};

struct v3dv_copy_query_results_cpu_job_info {
   struct v3dv_query_pool *pool;
   uint32_t first;
   uint32_t count;
   struct v3dv_buffer *dst;
   uint32_t offset;
   uint32_t stride;
   VkQueryResultFlags flags;
};

struct v3dv_event_set_cpu_job_info {
   struct v3dv_event *event;
   int state;
};

struct v3dv_event_wait_cpu_job_info {
   /* List of events to wait on */
   uint32_t event_count;
   struct v3dv_event **events;

   /* Whether any postponed jobs after the wait should wait on semaphores */
   bool sem_wait;
};

struct v3dv_clear_attachments_cpu_job_info {
   uint32_t attachment_count;
   VkClearAttachment attachments[V3D_MAX_DRAW_BUFFERS + 1]; /* 4 color + D/S */
   uint32_t rect_count;
   VkClearRect *rects;
};

struct v3dv_copy_buffer_to_image_cpu_job_info {
   struct v3dv_image *image;
   struct v3dv_buffer *buffer;
   uint32_t buffer_offset;
   uint32_t buffer_stride;
   uint32_t buffer_layer_stride;
   VkOffset3D image_offset;
   VkExtent3D image_extent;
   uint32_t mip_level;
   uint32_t base_layer;
   uint32_t layer_count;
};

struct v3dv_csd_indirect_cpu_job_info {
   struct v3dv_buffer *buffer;
   uint32_t offset;
   struct v3dv_job *csd_job;
   uint32_t wg_size;
   uint32_t *wg_uniform_offsets[3];
   bool needs_wg_uniform_rewrite;
};

struct v3dv_timestamp_query_cpu_job_info {
   struct v3dv_query_pool *pool;
   uint32_t query;
};

struct v3dv_job {
   struct list_head list_link;

   /* We only create job clones when executing secondary command buffers into
    * primaries. These clones don't make deep copies of the original object
    * so we want to flag them to avoid freeing resources they don't own.
    */
   bool is_clone;

   enum v3dv_job_type type;

   struct v3dv_device *device;

   struct v3dv_cmd_buffer *cmd_buffer;

   struct v3dv_cl bcl;
   struct v3dv_cl rcl;
   struct v3dv_cl indirect;

   /* Set of all BOs referenced by the job. This will be used for making
    * the list of BOs that the kernel will need to have paged in to
    * execute our job.
    */
   struct set *bos;
   uint32_t bo_count;

   struct v3dv_bo *tile_alloc;
   struct v3dv_bo *tile_state;

   bool tmu_dirty_rcl;

   uint32_t first_subpass;

   /* When the current subpass is split into multiple jobs, this flag is set
    * to true for any jobs after the first in the same subpass.
    */
   bool is_subpass_continue;

   /* If this job is the last job emitted for a subpass. */
   bool is_subpass_finish;

   struct v3dv_frame_tiling frame_tiling;

   enum v3dv_ez_state ez_state;
   enum v3dv_ez_state first_ez_state;

   /* Number of draw calls recorded into the job */
   uint32_t draw_count;

   /* A flag indicating whether we want to flush every draw separately. This
    * can be used for debugging, or for cases where special circumstances
    * require this behavior.
    */
   bool always_flush;

   /* Whether we need to serialize this job in our command stream */
   bool serialize;

   /* If this is a CL job, whether we should sync before binning */
   bool needs_bcl_sync;

   /* Job specs for CPU jobs */
   union {
      struct v3dv_reset_query_cpu_job_info          query_reset;
      struct v3dv_end_query_cpu_job_info            query_end;
      struct v3dv_copy_query_results_cpu_job_info   query_copy_results;
      struct v3dv_event_set_cpu_job_info            event_set;
      struct v3dv_event_wait_cpu_job_info           event_wait;
      struct v3dv_clear_attachments_cpu_job_info    clear_attachments;
      struct v3dv_copy_buffer_to_image_cpu_job_info copy_buffer_to_image;
      struct v3dv_csd_indirect_cpu_job_info         csd_indirect;
      struct v3dv_timestamp_query_cpu_job_info      query_timestamp;
   } cpu;

   /* Job specs for TFU jobs */
   struct drm_v3d_submit_tfu tfu;

   /* Job specs for CSD jobs */
   struct {
      struct v3dv_bo *shared_memory;
      uint32_t wg_count[3];
      struct drm_v3d_submit_csd submit;
   } csd;
};

void v3dv_job_init(struct v3dv_job *job,
                   enum v3dv_job_type type,
                   struct v3dv_device *device,
                   struct v3dv_cmd_buffer *cmd_buffer,
                   int32_t subpass_idx);
void v3dv_job_destroy(struct v3dv_job *job);
void v3dv_job_add_bo(struct v3dv_job *job, struct v3dv_bo *bo);
void v3dv_job_emit_binning_flush(struct v3dv_job *job);
void v3dv_job_start_frame(struct v3dv_job *job,
                          uint32_t width,
                          uint32_t height,
                          uint32_t layers,
                          uint32_t render_target_count,
                          uint8_t max_internal_bpp,
                          bool msaa);
struct v3dv_job *v3dv_cmd_buffer_create_cpu_job(struct v3dv_device *device,
                                                enum v3dv_job_type type,
                                                struct v3dv_cmd_buffer *cmd_buffer,
                                                uint32_t subpass_idx);

struct v3dv_vertex_binding {
   struct v3dv_buffer *buffer;
   VkDeviceSize offset;
};

struct v3dv_descriptor_state {
   struct v3dv_descriptor_set *descriptor_sets[MAX_SETS];
   uint32_t valid;
   uint32_t dynamic_offsets[MAX_DYNAMIC_BUFFERS];
};

struct v3dv_cmd_buffer_state {
   struct v3dv_render_pass *pass;
   struct v3dv_framebuffer *framebuffer;
   VkRect2D render_area;

   /* Current job being recorded */
   struct v3dv_job *job;

   uint32_t subpass_idx;

   struct v3dv_pipeline *pipeline;
   struct v3dv_descriptor_state descriptor_state[2];

   struct v3dv_dynamic_state dynamic;
   uint32_t dirty;

   /* Current clip window. We use this to check whether we have an active
    * scissor, since in that case we can't use TLB clears and need to fallback
    * to drawing rects.
    */
   VkRect2D clip_window;

   /* Whether our render area is aligned to tile boundaries. If this is false
    * then we have tiles that are only partially covered by the render area,
    * and therefore, we need to be careful with our loads and stores so we don't
    * modify pixels for the tile area that is not covered by the render area.
    * This means, for example, that we can't use the TLB to clear, since that
    * always clears full tiles.
    */
   bool tile_aligned_render_area;

   uint32_t attachment_alloc_count;
   struct v3dv_cmd_buffer_attachment_state *attachments;

   struct v3dv_vertex_binding vertex_bindings[MAX_VBS];

   struct {
      VkBuffer buffer;
      VkDeviceSize offset;
      uint8_t index_size;
   } index_buffer;

   /* Current uniforms */
   struct {
      struct v3dv_cl_reloc vs_bin;
      struct v3dv_cl_reloc vs;
      struct v3dv_cl_reloc fs;
   } uniforms;

   /* Used to flag OOM conditions during command buffer recording */
   bool oom;

   /* Whether we have recorded a pipeline barrier that we still need to
    * process.
    */
   bool has_barrier;
   bool has_bcl_barrier;

   /* Secondary command buffer state */
   struct {
      bool occlusion_query_enable;
   } inheritance;

   /* Command buffer state saved during a meta operation */
   struct {
      uint32_t subpass_idx;
      VkRenderPass pass;
      VkPipeline pipeline;
      VkFramebuffer framebuffer;

      uint32_t attachment_alloc_count;
      uint32_t attachment_count;
      struct v3dv_cmd_buffer_attachment_state *attachments;

      bool tile_aligned_render_area;
      VkRect2D render_area;

      struct v3dv_dynamic_state dynamic;

      struct v3dv_descriptor_state descriptor_state;
      bool has_descriptor_state;

      uint32_t push_constants[MAX_PUSH_CONSTANTS_SIZE / 4];
   } meta;

   /* Command buffer state for queries */
   struct {
      /* A list of vkCmdQueryEnd commands recorded in the command buffer during
       * a render pass. We queue these here and then schedule the corresponding
       * CPU jobs for them at the time we finish the GPU job in which they have
       * been recorded.
       */
      struct {
         uint32_t used_count;
         uint32_t alloc_count;
         struct v3dv_end_query_cpu_job_info *states;
      } end;

      /* This is not NULL if we have an active query, that is, we have called
       * vkCmdBeginQuery but not vkCmdEndQuery.
       */
      struct v3dv_bo *active_query;
   } query;
};

/* The following struct represents the info from a descriptor that we store on
 * the host memory. They are mostly links to other existing vulkan objects,
 * like the image_view in order to access to swizzle info, or the buffer used
 * for a UBO/SSBO, for example.
 *
 * FIXME: revisit if makes sense to just move everything that would be needed
 * from a descriptor to the bo.
 */
struct v3dv_descriptor {
   VkDescriptorType type;

   union {
      struct {
         struct v3dv_image_view *image_view;
         struct v3dv_sampler *sampler;
      };

      struct {
         struct v3dv_buffer *buffer;
         uint32_t offset;
         uint32_t range;
      };

      struct v3dv_buffer_view *buffer_view;
   };
};

/* The following v3dv_xxx_descriptor structs represent descriptor info that we
 * upload to a bo, specifically a subregion of the descriptor pool bo.
 *
 * The general rule that we apply right now to decide which info goes to such
 * bo is that we upload those that are referenced by an address when emitting
 * a packet, so needed to be uploaded to an bo in any case.
 *
 * Note that these structs are mostly helpers that improve the semantics when
 * doing all that, but we could do as other mesa vulkan drivers and just
 * upload the info we know it is expected based on the context.
 *
 * Also note that the sizes are aligned, as there is an alignment requirement
 * for addresses.
 */
struct v3dv_sampled_image_descriptor {
   uint8_t texture_state[cl_aligned_packet_length(TEXTURE_SHADER_STATE, 32)];
};

struct v3dv_sampler_descriptor {
   uint8_t sampler_state[cl_aligned_packet_length(SAMPLER_STATE, 32)];
};

struct v3dv_combined_image_sampler_descriptor {
   uint8_t texture_state[cl_aligned_packet_length(TEXTURE_SHADER_STATE, 32)];
   uint8_t sampler_state[cl_aligned_packet_length(SAMPLER_STATE, 32)];
};

/* Aux struct as it is really common to have a pair bo/address. Called
 * resource because it is really likely that we would need something like that
 * if we work on reuse the same bo at different points (like the shader
 * assembly).
 */
struct v3dv_resource {
   struct v3dv_bo *bo;
   uint32_t offset;
};

struct v3dv_query {
   bool maybe_available;
   union {
      struct v3dv_bo *bo; /* Used by GPU queries (occlusion) */
      uint64_t value; /* Used by CPU queries (timestamp) */
   };
};

struct v3dv_query_pool {
   struct vk_object_base base;

   VkQueryType query_type;
   uint32_t query_count;
   struct v3dv_query *queries;
};

VkResult v3dv_get_query_pool_results_cpu(struct v3dv_device *device,
                                         struct v3dv_query_pool *pool,
                                         uint32_t first,
                                         uint32_t count,
                                         void *data,
                                         VkDeviceSize stride,
                                         VkQueryResultFlags flags);

typedef void (*v3dv_cmd_buffer_private_obj_destroy_cb)(VkDevice device,
                                                       uint64_t pobj,
                                                       VkAllocationCallbacks *alloc);
struct v3dv_cmd_buffer_private_obj {
   struct list_head list_link;
   uint64_t obj;
   v3dv_cmd_buffer_private_obj_destroy_cb destroy_cb;
};

struct v3dv_cmd_buffer {
   struct vk_object_base base;

   struct v3dv_device *device;

   struct v3dv_cmd_pool *pool;
   struct list_head pool_link;

   /* Used at submit time to link command buffers in the submission that have
    * spawned wait threads, so we can then wait on all of them to complete
    * before we process any signal sempahores or fences.
    */
   struct list_head list_link;

   VkCommandBufferUsageFlags usage_flags;
   VkCommandBufferLevel level;

   enum v3dv_cmd_buffer_status status;

   struct v3dv_cmd_buffer_state state;

   uint32_t push_constants_data[MAX_PUSH_CONSTANTS_SIZE / 4];
   struct v3dv_resource push_constants_resource;

   /* Collection of Vulkan objects created internally by the driver (typically
    * during recording of meta operations) that are part of the command buffer
    * and should be destroyed with it.
    */
   struct list_head private_objs; /* v3dv_cmd_buffer_private_obj */

   /* Per-command buffer resources for meta operations. */
   struct {
      struct {
         /* The current descriptor pool for blit sources */
         VkDescriptorPool dspool;
      } blit;
      struct {
         /* The current descriptor pool for texel buffer copy sources */
         VkDescriptorPool dspool;
      } texel_buffer_copy;
   } meta;

   /* List of jobs in the command buffer. For primary command buffers it
    * represents the jobs we want to submit to the GPU. For secondary command
    * buffers it represents jobs that will be merged into a primary command
    * buffer via vkCmdExecuteCommands.
    */
   struct list_head jobs;
};

struct v3dv_job *v3dv_cmd_buffer_start_job(struct v3dv_cmd_buffer *cmd_buffer,
                                           int32_t subpass_idx,
                                           enum v3dv_job_type type);
void v3dv_cmd_buffer_finish_job(struct v3dv_cmd_buffer *cmd_buffer);

struct v3dv_job *v3dv_cmd_buffer_subpass_start(struct v3dv_cmd_buffer *cmd_buffer,
                                               uint32_t subpass_idx);
struct v3dv_job *v3dv_cmd_buffer_subpass_resume(struct v3dv_cmd_buffer *cmd_buffer,
                                                uint32_t subpass_idx);

void v3dv_cmd_buffer_subpass_finish(struct v3dv_cmd_buffer *cmd_buffer);

void v3dv_cmd_buffer_meta_state_push(struct v3dv_cmd_buffer *cmd_buffer,
                                     bool push_descriptor_state);
void v3dv_cmd_buffer_meta_state_pop(struct v3dv_cmd_buffer *cmd_buffer,
                                    uint32_t dirty_dynamic_state,
                                    bool needs_subpass_resume);

void v3dv_render_pass_setup_render_target(struct v3dv_cmd_buffer *cmd_buffer,
                                          int rt,
                                          uint32_t *rt_bpp,
                                          uint32_t *rt_type,
                                          uint32_t *rt_clamp);

void v3dv_cmd_buffer_reset_queries(struct v3dv_cmd_buffer *cmd_buffer,
                                   struct v3dv_query_pool *pool,
                                   uint32_t first,
                                   uint32_t count);

void v3dv_cmd_buffer_begin_query(struct v3dv_cmd_buffer *cmd_buffer,
                                 struct v3dv_query_pool *pool,
                                 uint32_t query,
                                 VkQueryControlFlags flags);

void v3dv_cmd_buffer_end_query(struct v3dv_cmd_buffer *cmd_buffer,
                               struct v3dv_query_pool *pool,
                               uint32_t query);

void v3dv_cmd_buffer_copy_query_results(struct v3dv_cmd_buffer *cmd_buffer,
                                        struct v3dv_query_pool *pool,
                                        uint32_t first,
                                        uint32_t count,
                                        struct v3dv_buffer *dst,
                                        uint32_t offset,
                                        uint32_t stride,
                                        VkQueryResultFlags flags);

void v3dv_cmd_buffer_add_tfu_job(struct v3dv_cmd_buffer *cmd_buffer,
                                 struct drm_v3d_submit_tfu *tfu);

void v3dv_cmd_buffer_rewrite_indirect_csd_job(struct v3dv_csd_indirect_cpu_job_info *info,
                                              const uint32_t *wg_counts);

void v3dv_cmd_buffer_add_private_obj(struct v3dv_cmd_buffer *cmd_buffer,
                                     uint64_t obj,
                                     v3dv_cmd_buffer_private_obj_destroy_cb destroy_cb);

struct v3dv_semaphore {
   struct vk_object_base base;

   /* A syncobject handle associated with this semaphore */
   uint32_t sync;

   /* The file handle of a fence that we imported into our syncobject */
   int32_t fd;
};

struct v3dv_fence {
   struct vk_object_base base;

   /* A syncobject handle associated with this fence */
   uint32_t sync;

   /* The file handle of a fence that we imported into our syncobject */
   int32_t fd;
};

struct v3dv_event {
   struct vk_object_base base;
   int state;
};

struct v3dv_shader_module {
   struct vk_object_base base;

   /* A NIR shader. We create NIR modules for shaders that are generated
    * internally by the driver.
    */
   struct nir_shader *nir;

   /* A SPIR-V shader */
   unsigned char sha1[20];
   uint32_t size;
   char data[0];
};

/* FIXME: the same function at anv, radv and tu, perhaps create common
 * place?
 */
static inline gl_shader_stage
vk_to_mesa_shader_stage(VkShaderStageFlagBits vk_stage)
{
   assert(__builtin_popcount(vk_stage) == 1);
   return ffs(vk_stage) - 1;
}

struct v3dv_shader_variant {
   uint32_t ref_cnt;

   gl_shader_stage stage;
   bool is_coord;

   /* v3d_key used to compile the variant. Sometimes we can just skip the
    * pipeline caches, and look using this.
    */
   union {
      struct v3d_key base;
      struct v3d_vs_key vs;
      struct v3d_fs_key fs;
   } key;
   uint32_t v3d_key_size;

   /* key for the pipeline cache, it is p_stage shader_sha1 + v3d compiler
    * sha1
    */
   unsigned char variant_sha1[20];

   union {
      struct v3d_prog_data *base;
      struct v3d_vs_prog_data *vs;
      struct v3d_fs_prog_data *fs;
      struct v3d_compute_prog_data *cs;
   } prog_data;

   /* We explicitly save the prog_data_size as it would make easier to
    * serialize
    */
   uint32_t prog_data_size;
   /* FIXME: using one bo per shader. Eventually we would be interested on
    * reusing the same bo for all the shaders, like a bo per v3dv_pipeline for
    * shaders.
    */
   struct v3dv_bo *assembly_bo;
   uint32_t qpu_insts_size;
};

/*
 * Per-stage info for each stage, useful so shader_module_compile_to_nir and
 * other methods doesn't have so many parameters.
 *
 * FIXME: for the case of the coordinate shader and the vertex shader, module,
 * entrypoint, spec_info and nir are the same. There are also info only
 * relevant to some stages. But seemed too much a hassle to create a new
 * struct only to handle that. Revisit if such kind of info starts to grow.
 */
struct v3dv_pipeline_stage {
   struct v3dv_pipeline *pipeline;

   gl_shader_stage stage;
   /* FIXME: is_coord only make sense if stage == MESA_SHADER_VERTEX. Perhaps
    * a stage base/vs/fs as keys and prog_data?
    */
   bool is_coord;

   const struct v3dv_shader_module *module;
   const char *entrypoint;
   const VkSpecializationInfo *spec_info;

   nir_shader *nir;

   /* The following is the combined hash of module+entrypoint+spec_info+nir */
   unsigned char shader_sha1[20];

   /** A name for this program, so you can track it in shader-db output. */
   uint32_t program_id;
   /** How many variants of this program were compiled, for shader-db. */
   uint32_t compiled_variant_count;

   /* The following are the default v3d_key populated using
    * VkCreateGraphicsPipelineCreateInfo. Variants will be created tweaking
    * them, so we don't need to maintain a copy of that create info struct
    * around
    */
   union {
      struct v3d_key base;
      struct v3d_vs_key vs;
      struct v3d_fs_key fs;
   } key;

   struct v3dv_shader_variant*current_variant;

   /* FIXME: only make sense on vs, so perhaps a v3dv key like radv? or a kind
    * of pipe_draw_info
    */
   enum pipe_prim_type topology;
};

/* FIXME: although the full vpm_config is not required at this point, as we
 * don't plan to initially support GS, it is more readable and serves as a
 * placeholder, to have the struct and fill it with default values.
 */
struct vpm_config {
   uint32_t As;
   uint32_t Vc;
   uint32_t Gs;
   uint32_t Gd;
   uint32_t Gv;
   uint32_t Ve;
   uint32_t gs_width;
};

/* We are using the descriptor pool entry for two things:
 * * Track the allocated sets, so we can properly free it if needed
 * * Track the suballocated pool bo regions, so if some descriptor set is
 *   freed, the gap could be reallocated later.
 *
 * Those only make sense if the pool was not created with the flag
 * VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
 */
struct v3dv_descriptor_pool_entry
{
   struct v3dv_descriptor_set *set;
   /* Offset and size of the subregion allocated for this entry from the
    * pool->bo
    */
   uint32_t offset;
   uint32_t size;
};

struct v3dv_descriptor_pool {
   struct vk_object_base base;

   /* If this descriptor pool has been allocated for the driver for internal
    * use, typically to implement meta operations.
    */
   bool is_driver_internal;

   struct v3dv_bo *bo;
   /* Current offset at the descriptor bo. 0 means that we didn't use it for
    * any descriptor. If the descriptor bo is NULL, current offset is
    * meaningless
    */
   uint32_t current_offset;

   /* If VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT is not set the
    * descriptor sets are handled as a whole as pool memory and handled by the
    * following pointers. If set, they are not used, and individually
    * descriptor sets are allocated/freed.
    */
   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;

   uint32_t entry_count;
   uint32_t max_entry_count;
   struct v3dv_descriptor_pool_entry entries[0];
};

struct v3dv_descriptor_set {
   struct vk_object_base base;

   struct v3dv_descriptor_pool *pool;

   const struct v3dv_descriptor_set_layout *layout;

   /* Offset relative to the descriptor pool bo for this set */
   uint32_t base_offset;

   /* The descriptors below can be indexed (set/binding) using the set_layout
    */
   struct v3dv_descriptor descriptors[0];
};

struct v3dv_descriptor_set_binding_layout {
   VkDescriptorType type;

   /* Number of array elements in this binding */
   uint32_t array_size;

   /* Index into the flattend descriptor set */
   uint32_t descriptor_index;

   uint32_t dynamic_offset_count;
   uint32_t dynamic_offset_index;

   /* Offset into the descriptor set where this descriptor lives (final offset
    * on the descriptor bo need to take into account set->base_offset)
    */
   uint32_t descriptor_offset;

   /* Offset in the v3dv_descriptor_set_layout of the immutable samplers, or 0
    * if there are no immutable samplers.
    */
   uint32_t immutable_samplers_offset;
};

struct v3dv_descriptor_set_layout {
   struct vk_object_base base;

   VkDescriptorSetLayoutCreateFlags flags;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Total bo size needed for this descriptor set
    */
   uint32_t bo_size;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;

   /* Number of descriptors in this descriptor set */
   uint32_t descriptor_count;

   /* Number of dynamic offsets used by this descriptor set */
   uint16_t dynamic_offset_count;

   /* Bindings in this descriptor set */
   struct v3dv_descriptor_set_binding_layout binding[0];
};

struct v3dv_pipeline_layout {
   struct vk_object_base base;

   struct {
      struct v3dv_descriptor_set_layout *layout;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t num_sets;
   uint32_t dynamic_offset_count;

   uint32_t push_constant_size;
};

struct v3dv_descriptor_map {
   /* TODO: avoid fixed size array/justify the size */
   unsigned num_desc; /* Number of descriptors  */
   int set[64];
   int binding[64];
   int array_index[64];
   int array_size[64];

   /* NOTE: the following is only for sampler, but this is the easier place to
    * put it.
    */
   uint8_t return_size[64];
};

struct v3dv_sampler {
   struct vk_object_base base;

   bool compare_enable;
   bool unnormalized_coordinates;
   bool clamp_to_transparent_black_border;

   /* Prepacked SAMPLER_STATE, that is referenced as part of the tmu
    * configuration. If needed it will be copied to the descriptor info during
    * UpdateDescriptorSets
    */
   uint8_t sampler_state[cl_packet_length(SAMPLER_STATE)];
};

/* We keep two special values for the sampler idx that represents exactly when a
 * sampler is not needed/provided. The main use is that even if we don't have
 * sampler, we still need to do the output unpacking (through
 * nir_lower_tex). The easier way to do this is to add those special "no
 * sampler" in the sampler_map, and then use the proper unpacking for that
 * case.
 *
 * We have one when we want a 16bit output size, and other when we want a
 * 32bit output size. We use the info coming from the RelaxedPrecision
 * decoration to decide between one and the other.
 */
#define V3DV_NO_SAMPLER_16BIT_IDX 0
#define V3DV_NO_SAMPLER_32BIT_IDX 1

/*
 * Following two methods are using on the combined to/from texture/sampler
 * indices maps at v3dv_pipeline.
 */
static inline uint32_t
v3dv_pipeline_combined_index_key_create(uint32_t texture_index,
                                        uint32_t sampler_index)
{
   return texture_index << 24 | sampler_index;
}

static inline void
v3dv_pipeline_combined_index_key_unpack(uint32_t combined_index_key,
                                        uint32_t *texture_index,
                                        uint32_t *sampler_index)
{
   uint32_t texture = combined_index_key >> 24;
   uint32_t sampler = combined_index_key & 0xffffff;

   if (texture_index)
      *texture_index = texture;

   if (sampler_index)
      *sampler_index = sampler;
}

struct v3dv_pipeline {
   struct vk_object_base base;

   struct v3dv_device *device;

   VkShaderStageFlags active_stages;

   struct v3dv_render_pass *pass;
   struct v3dv_subpass *subpass;

   /* Note: We can't use just a MESA_SHADER_STAGES array as we need to track
    * too the coordinate shader
    */
   struct v3dv_pipeline_stage *vs;
   struct v3dv_pipeline_stage *vs_bin;
   struct v3dv_pipeline_stage *fs;
   struct v3dv_pipeline_stage *cs;

   /* Spilling memory requirements */
   struct {
      struct v3dv_bo *bo;
      uint32_t size_per_thread;
   } spill;

   struct v3dv_dynamic_state dynamic_state;

   struct v3dv_pipeline_layout *layout;

   enum v3dv_ez_state ez_state;

   bool msaa;
   bool sample_rate_shading;
   uint32_t sample_mask;

   bool primitive_restart;

   /* Accessed by binding. So vb[binding]->stride is the stride of the vertex
    * array with such binding
    */
   struct v3dv_pipeline_vertex_binding {
      uint32_t stride;
      uint32_t instance_divisor;
   } vb[MAX_VBS];
   uint32_t vb_count;

   /* Note that a lot of info from VkVertexInputAttributeDescription is
    * already prepacked, so here we are only storing those that need recheck
    * later. The array must be indexed by driver location, since that is the
    * order in which we need to emit the attributes.
    */
   struct v3dv_pipeline_vertex_attrib {
      uint32_t binding;
      uint32_t offset;
      VkFormat vk_format;
   } va[MAX_VERTEX_ATTRIBS];
   uint32_t va_count;

   struct v3dv_descriptor_map ubo_map;
   struct v3dv_descriptor_map ssbo_map;

   struct v3dv_descriptor_map sampler_map;
   struct v3dv_descriptor_map texture_map;

   /* FIXME: this bo is another candidate to data to be uploaded using a
    * resource manager, instead of a individual bo
    */
   struct v3dv_bo *default_attribute_values;

   struct vpm_config vpm_cfg;
   struct vpm_config vpm_cfg_bin;

   /* If the pipeline should emit any of the stencil configuration packets */
   bool emit_stencil_cfg[2];

   /* If the pipeline is using push constants */
   bool use_push_constants;

   /* Blend state */
   struct {
      /* Per-RT bit mask with blend enables */
      uint8_t enables;
      /* Per-RT prepacked blend config packets */
      uint8_t cfg[V3D_MAX_DRAW_BUFFERS][cl_packet_length(BLEND_CFG)];
      /* Flag indicating whether the blend factors in use require
       * color constants.
       */
      bool needs_color_constants;
      /* Mask with enabled color channels for each RT (4 bits per RT) */
      uint32_t color_write_masks;
   } blend;

   /* Depth bias */
   struct {
      bool enabled;
      bool is_z16;
   } depth_bias;

   /* Packets prepacked during pipeline creation
    */
   uint8_t cfg_bits[cl_packet_length(CFG_BITS)];
   uint8_t shader_state_record[cl_packet_length(GL_SHADER_STATE_RECORD)];
   uint8_t vcm_cache_size[cl_packet_length(VCM_CACHE_SIZE)];
   uint8_t vertex_attrs[cl_packet_length(GL_SHADER_STATE_ATTRIBUTE_RECORD) *
                        MAX_VERTEX_ATTRIBS];
   uint8_t stencil_cfg[2][cl_packet_length(STENCIL_CFG)];
};

static inline VkPipelineBindPoint
v3dv_pipeline_get_binding_point(struct v3dv_pipeline *pipeline)
{
   assert(pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT ||
          !(pipeline->active_stages & VK_SHADER_STAGE_COMPUTE_BIT));
   return pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT ?
      VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
}

const nir_shader_compiler_options *v3dv_pipeline_get_nir_options(void);

static inline uint32_t
v3dv_zs_buffer_from_aspect_bits(VkImageAspectFlags aspects)
{
   const VkImageAspectFlags zs_aspects =
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
   const VkImageAspectFlags filtered_aspects = aspects & zs_aspects;

   if (filtered_aspects == zs_aspects)
      return ZSTENCIL;
   else if (filtered_aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
      return Z;
   else if (filtered_aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
      return STENCIL;
   else
      return NONE;
}

static inline uint32_t
v3dv_zs_buffer_from_vk_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_D16_UNORM_S8_UINT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return ZSTENCIL;
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_X8_D24_UNORM_PACK32:
      return Z;
   case VK_FORMAT_S8_UINT:
      return STENCIL;
   default:
      return NONE;
   }
}

static inline uint32_t
v3dv_zs_buffer(bool depth, bool stencil)
{
   if (depth && stencil)
      return ZSTENCIL;
   else if (depth)
      return Z;
   else if (stencil)
      return STENCIL;
   return NONE;
}

static inline uint8_t
v3dv_get_internal_depth_type(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_D16_UNORM:
      return V3D_INTERNAL_TYPE_DEPTH_16;
   case VK_FORMAT_D32_SFLOAT:
      return V3D_INTERNAL_TYPE_DEPTH_32F;
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return V3D_INTERNAL_TYPE_DEPTH_24;
   default:
      unreachable("Invalid depth format");
      break;
   }
}

uint32_t v3dv_physical_device_api_version(struct v3dv_physical_device *dev);
uint32_t v3dv_physical_device_vendor_id(struct v3dv_physical_device *dev);
uint32_t v3dv_physical_device_device_id(struct v3dv_physical_device *dev);

int v3dv_get_instance_entrypoint_index(const char *name);
int v3dv_get_device_entrypoint_index(const char *name);
int v3dv_get_physical_device_entrypoint_index(const char *name);

const char *v3dv_get_instance_entry_name(int index);
const char *v3dv_get_physical_device_entry_name(int index);
const char *v3dv_get_device_entry_name(int index);

bool
v3dv_instance_entrypoint_is_enabled(int index, uint32_t core_version,
                                    const struct v3dv_instance_extension_table *instance);
bool
v3dv_physical_device_entrypoint_is_enabled(int index, uint32_t core_version,
                                           const struct v3dv_instance_extension_table *instance);
bool
v3dv_device_entrypoint_is_enabled(int index, uint32_t core_version,
                                  const struct v3dv_instance_extension_table *instance,
                                  const struct v3dv_device_extension_table *device);

void *v3dv_lookup_entrypoint(const struct v3d_device_info *devinfo,
                             const char *name);

VkResult __vk_errorf(struct v3dv_instance *instance, VkResult error,
                     const char *file, int line,
                     const char *format, ...);

#define vk_error(instance, error) __vk_errorf(instance, error, __FILE__, __LINE__, NULL);
#define vk_errorf(instance, error, format, ...) __vk_errorf(instance, error, __FILE__, __LINE__, format, ## __VA_ARGS__);

#ifdef DEBUG
#define v3dv_debug_ignored_stype(sType) \
   fprintf(stderr, "%s: ignored VkStructureType %u:%s\n\n", __func__, (sType), vk_StructureType_to_str(sType))
#else
#define v3dv_debug_ignored_stype(sType)
#endif

const struct v3dv_format *v3dv_get_format(VkFormat);
const uint8_t *v3dv_get_format_swizzle(VkFormat f);
void v3dv_get_internal_type_bpp_for_output_format(uint32_t format, uint32_t *type, uint32_t *bpp);
uint8_t v3dv_get_tex_return_size(const struct v3dv_format *vf, bool compare_enable);
bool v3dv_tfu_supports_tex_format(const struct v3d_device_info *devinfo,
                                  uint32_t tex_format);
const struct v3dv_format *
v3dv_get_compatible_tfu_format(const struct v3d_device_info *devinfo,
                               uint32_t bpp, VkFormat *out_vk_format);
bool v3dv_buffer_format_supports_features(VkFormat vk_format,
                                          VkFormatFeatureFlags features);
bool v3dv_format_supports_tlb_resolve(const struct v3dv_format *format);

uint32_t v3d_utile_width(int cpp);
uint32_t v3d_utile_height(int cpp);

void v3d_load_tiled_image(void *dst, uint32_t dst_stride,
                          void *src, uint32_t src_stride,
                          enum v3d_tiling_mode tiling_format,
                          int cpp, uint32_t image_h,
                          const struct pipe_box *box);

void v3d_store_tiled_image(void *dst, uint32_t dst_stride,
                           void *src, uint32_t src_stride,
                           enum v3d_tiling_mode tiling_format,
                           int cpp, uint32_t image_h,
                           const struct pipe_box *box);

struct v3dv_cl_reloc v3dv_write_uniforms(struct v3dv_cmd_buffer *cmd_buffer,
                                         struct v3dv_pipeline_stage *p_stage);
struct v3dv_cl_reloc v3dv_write_uniforms_wg_offsets(struct v3dv_cmd_buffer *cmd_buffer,
                                                    struct v3dv_pipeline_stage *p_stage,
                                                    uint32_t **wg_count_offsets);

struct v3dv_shader_variant *
v3dv_get_shader_variant(struct v3dv_pipeline_stage *p_stage,
                        struct v3dv_pipeline_cache *cache,
                        struct v3d_key *key,
                        size_t key_size,
                        const VkAllocationCallbacks *pAllocator,
                        VkResult *out_vk_result);

struct v3dv_shader_variant *
v3dv_shader_variant_create(struct v3dv_device *device,
                           gl_shader_stage stage,
                           bool is_coord,
                           const unsigned char *variant_sha1,
                           const struct v3d_key *key,
                           uint32_t key_size,
                           struct v3d_prog_data *prog_data,
                           uint32_t prog_data_size,
                           const uint64_t *qpu_insts,
                           uint32_t qpu_insts_size,
                           VkResult *out_vk_result);

void
v3dv_shader_variant_destroy(struct v3dv_device *device,
                            struct v3dv_shader_variant *variant);

static inline void
v3dv_shader_variant_ref(struct v3dv_shader_variant *variant)
{
   assert(variant && variant->ref_cnt >= 1);
   p_atomic_inc(&variant->ref_cnt);
}

static inline void
v3dv_shader_variant_unref(struct v3dv_device *device,
                          struct v3dv_shader_variant *variant)
{
   assert(variant && variant->ref_cnt >= 1);
   if (p_atomic_dec_zero(&variant->ref_cnt))
      v3dv_shader_variant_destroy(device, variant);
}

struct v3dv_descriptor *
v3dv_descriptor_map_get_descriptor(struct v3dv_descriptor_state *descriptor_state,
                                   struct v3dv_descriptor_map *map,
                                   struct v3dv_pipeline_layout *pipeline_layout,
                                   uint32_t index,
                                   uint32_t *dynamic_offset);

const struct v3dv_sampler *
v3dv_descriptor_map_get_sampler(struct v3dv_descriptor_state *descriptor_state,
                                struct v3dv_descriptor_map *map,
                                struct v3dv_pipeline_layout *pipeline_layout,
                                uint32_t index);

struct v3dv_cl_reloc
v3dv_descriptor_map_get_sampler_state(struct v3dv_descriptor_state *descriptor_state,
                                      struct v3dv_descriptor_map *map,
                                      struct v3dv_pipeline_layout *pipeline_layout,
                                      uint32_t index);

struct v3dv_cl_reloc
v3dv_descriptor_map_get_texture_shader_state(struct v3dv_descriptor_state *descriptor_state,
                                             struct v3dv_descriptor_map *map,
                                             struct v3dv_pipeline_layout *pipeline_layout,
                                             uint32_t index);

const struct v3dv_format*
v3dv_descriptor_map_get_texture_format(struct v3dv_descriptor_state *descriptor_state,
                                       struct v3dv_descriptor_map *map,
                                       struct v3dv_pipeline_layout *pipeline_layout,
                                       uint32_t index,
                                       VkFormat *out_vk_format);

struct v3dv_bo*
v3dv_descriptor_map_get_texture_bo(struct v3dv_descriptor_state *descriptor_state,
                                   struct v3dv_descriptor_map *map,
                                   struct v3dv_pipeline_layout *pipeline_layout,
                                   uint32_t index);

static inline const struct v3dv_sampler *
v3dv_immutable_samplers(const struct v3dv_descriptor_set_layout *set,
                        const struct v3dv_descriptor_set_binding_layout *binding)
{
   assert(binding->immutable_samplers_offset);
   return (const struct v3dv_sampler *) ((const char *) set + binding->immutable_samplers_offset);
}

void v3dv_pipeline_cache_init(struct v3dv_pipeline_cache *cache,
                              struct v3dv_device *device,
                              bool cache_enabled);

void v3dv_pipeline_cache_finish(struct v3dv_pipeline_cache *cache);

void v3dv_pipeline_cache_upload_nir(struct v3dv_pipeline *pipeline,
                                    struct v3dv_pipeline_cache *cache,
                                    nir_shader *nir,
                                    unsigned char sha1_key[20]);

nir_shader* v3dv_pipeline_cache_search_for_nir(struct v3dv_pipeline *pipeline,
                                               struct v3dv_pipeline_cache *cache,
                                               const nir_shader_compiler_options *nir_options,
                                               unsigned char sha1_key[20]);

struct v3dv_shader_variant*
v3dv_pipeline_cache_search_for_variant(struct v3dv_pipeline *pipeline,
                                       struct v3dv_pipeline_cache *cache,
                                       unsigned char sha1_key[20]);

void
v3dv_pipeline_cache_upload_variant(struct v3dv_pipeline *pipeline,
                                   struct v3dv_pipeline_cache *cache,
                                   struct v3dv_shader_variant  *variant);

void v3dv_shader_module_internal_init(struct v3dv_shader_module *module,
                                      nir_shader *nir);

#define V3DV_DEFINE_HANDLE_CASTS(__v3dv_type, __VkType)   \
                                                        \
   static inline struct __v3dv_type *                    \
   __v3dv_type ## _from_handle(__VkType _handle)         \
   {                                                    \
      return (struct __v3dv_type *) _handle;             \
   }                                                    \
                                                        \
   static inline __VkType                               \
   __v3dv_type ## _to_handle(struct __v3dv_type *_obj)    \
   {                                                    \
      return (__VkType) _obj;                           \
   }

#define V3DV_DEFINE_NONDISP_HANDLE_CASTS(__v3dv_type, __VkType)              \
                                                                           \
   static inline struct __v3dv_type *                                       \
   __v3dv_type ## _from_handle(__VkType _handle)                            \
   {                                                                       \
      return (struct __v3dv_type *)(uintptr_t) _handle;                     \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __v3dv_type ## _to_handle(struct __v3dv_type *_obj)                       \
   {                                                                       \
      return (__VkType)(uintptr_t) _obj;                                   \
   }

#define V3DV_FROM_HANDLE(__v3dv_type, __name, __handle)			\
   struct __v3dv_type *__name = __v3dv_type ## _from_handle(__handle)

V3DV_DEFINE_HANDLE_CASTS(v3dv_cmd_buffer, VkCommandBuffer)
V3DV_DEFINE_HANDLE_CASTS(v3dv_device, VkDevice)
V3DV_DEFINE_HANDLE_CASTS(v3dv_instance, VkInstance)
V3DV_DEFINE_HANDLE_CASTS(v3dv_physical_device, VkPhysicalDevice)
V3DV_DEFINE_HANDLE_CASTS(v3dv_queue, VkQueue)

V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_cmd_pool, VkCommandPool)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_buffer, VkBuffer)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_buffer_view, VkBufferView)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_device_memory, VkDeviceMemory)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_descriptor_pool, VkDescriptorPool)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_descriptor_set, VkDescriptorSet)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_descriptor_set_layout, VkDescriptorSetLayout)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_event, VkEvent)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_fence, VkFence)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_framebuffer, VkFramebuffer)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_image, VkImage)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_image_view, VkImageView)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_pipeline, VkPipeline)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_pipeline_cache, VkPipelineCache)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_pipeline_layout, VkPipelineLayout)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_query_pool, VkQueryPool)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_render_pass, VkRenderPass)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_sampler, VkSampler)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_semaphore, VkSemaphore)
V3DV_DEFINE_NONDISP_HANDLE_CASTS(v3dv_shader_module, VkShaderModule)

/* This is defined as a macro so that it works for both
 * VkImageSubresourceRange and VkImageSubresourceLayers
 */
#define v3dv_layer_count(_image, _range) \
   ((_range)->layerCount == VK_REMAINING_ARRAY_LAYERS ? \
    (_image)->array_size - (_range)->baseArrayLayer : (_range)->layerCount)

#define v3dv_level_count(_image, _range) \
   ((_range)->levelCount == VK_REMAINING_MIP_LEVELS ? \
    (_image)->levels - (_range)->baseMipLevel : (_range)->levelCount)

static inline int
v3dv_ioctl(int fd, unsigned long request, void *arg)
{
   if (using_v3d_simulator)
      return v3d_simulator_ioctl(fd, request, arg);
   else
      return drmIoctl(fd, request, arg);
}

/* Flags OOM conditions in command buffer state.
 *
 * Note: notice that no-op jobs don't have a command buffer reference.
 */
static inline void
v3dv_flag_oom(struct v3dv_cmd_buffer *cmd_buffer, struct v3dv_job *job)
{
   if (cmd_buffer) {
      cmd_buffer->state.oom = true;
   } else {
      assert(job);
      if (job->cmd_buffer)
         job->cmd_buffer->state.oom = true;
   }
}

#define v3dv_return_if_oom(_cmd_buffer, _job) do {                  \
   const struct v3dv_cmd_buffer *__cmd_buffer = _cmd_buffer;        \
   if (__cmd_buffer && __cmd_buffer->state.oom)                     \
      return;                                                       \
   const struct v3dv_job *__job = _job;                             \
   if (__job && __job->cmd_buffer && __job->cmd_buffer->state.oom)  \
      return;                                                       \
} while(0)                                                          \

static inline uint32_t
u64_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(uint64_t));
}

static inline bool
u64_compare(const void *key1, const void *key2)
{
   return memcmp(key1, key2, sizeof(uint64_t)) == 0;
}

#endif /* V3DV_PRIVATE_H */
