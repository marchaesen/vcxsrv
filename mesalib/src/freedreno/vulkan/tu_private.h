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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef TU_PRIVATE_H
#define TU_PRIVATE_H

#include <assert.h>
#include <pthread.h>
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
#define VG(x)
#endif

#include "c11/threads.h"
#include "compiler/shader_enums.h"
#include "main/macros.h"
#include "util/list.h"
#include "util/macros.h"
#include "vk_alloc.h"
#include "vk_debug_report.h"
#include "wsi_common.h"

#include "drm/msm_drm.h"
#include "ir3/ir3_compiler.h"
#include "ir3/ir3_shader.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"
#include "a6xx.xml.h"

#include "tu_descriptor_set.h"
#include "tu_extensions.h"

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_intel.h>

#include "tu_entrypoints.h"

#define MAX_VBS 32
#define MAX_VERTEX_ATTRIBS 32
#define MAX_RTS 8
#define MAX_VSC_PIPES 32
#define MAX_VIEWPORTS 1
#define MAX_SCISSORS 16
#define MAX_DISCARD_RECTANGLES 4
#define MAX_PUSH_CONSTANTS_SIZE 128
#define MAX_PUSH_DESCRIPTORS 32
#define MAX_DYNAMIC_UNIFORM_BUFFERS 16
#define MAX_DYNAMIC_STORAGE_BUFFERS 8
#define MAX_DYNAMIC_BUFFERS                                                  \
   (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)
#define MAX_SAMPLES_LOG2 4
#define NUM_META_FS_KEYS 13
#define TU_MAX_DRM_DEVICES 8
#define MAX_VIEWS 8

#define NUM_DEPTH_CLEAR_PIPELINES 3

/*
 * This is the point we switch from using CP to compute shader
 * for certain buffer operations.
 */
#define TU_BUFFER_OPS_CS_THRESHOLD 4096

enum tu_mem_heap
{
   TU_MEM_HEAP_VRAM,
   TU_MEM_HEAP_VRAM_CPU_ACCESS,
   TU_MEM_HEAP_GTT,
   TU_MEM_HEAP_COUNT
};

enum tu_mem_type
{
   TU_MEM_TYPE_VRAM,
   TU_MEM_TYPE_GTT_WRITE_COMBINE,
   TU_MEM_TYPE_VRAM_CPU_ACCESS,
   TU_MEM_TYPE_GTT_CACHED,
   TU_MEM_TYPE_COUNT
};

#define tu_printflike(a, b) __attribute__((__format__(__printf__, a, b)))

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
tu_is_aligned(uintmax_t n, uintmax_t a)
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
tu_minify(uint32_t n, uint32_t levels)
{
   if (unlikely(n == 0))
      return 0;
   else
      return MAX2(n >> levels, 1);
}
static inline float
tu_clamp_f(float f, float min, float max)
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
tu_clear_mask(uint32_t *inout_mask, uint32_t clear_mask)
{
   if (*inout_mask & clear_mask) {
      *inout_mask &= ~clear_mask;
      return true;
   } else {
      return false;
   }
}

#define for_each_bit(b, dword)                                               \
   for (uint32_t __dword = (dword);                                          \
        (b) = __builtin_ffs(__dword) - 1, __dword; __dword &= ~(1 << (b)))

#define typed_memcpy(dest, src, count)                                       \
   ({                                                                        \
      STATIC_ASSERT(sizeof(*src) == sizeof(*dest));                          \
      memcpy((dest), (src), (count) * sizeof(*(src)));                       \
   })

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

struct tu_instance;

VkResult
__vk_errorf(struct tu_instance *instance,
            VkResult error,
            const char *file,
            int line,
            const char *format,
            ...);

#define vk_error(instance, error)                                            \
   __vk_errorf(instance, error, __FILE__, __LINE__, NULL);
#define vk_errorf(instance, error, format, ...)                              \
   __vk_errorf(instance, error, __FILE__, __LINE__, format, ##__VA_ARGS__);

void
__tu_finishme(const char *file, int line, const char *format, ...)
   tu_printflike(3, 4);
void
tu_loge(const char *format, ...) tu_printflike(1, 2);
void
tu_loge_v(const char *format, va_list va);
void
tu_logi(const char *format, ...) tu_printflike(1, 2);
void
tu_logi_v(const char *format, va_list va);

/**
 * Print a FINISHME message, including its source location.
 */
#define tu_finishme(format, ...)                                             \
   do {                                                                      \
      static bool reported = false;                                          \
      if (!reported) {                                                       \
         __tu_finishme(__FILE__, __LINE__, format, ##__VA_ARGS__);           \
         reported = true;                                                    \
      }                                                                      \
   } while (0)

/* A non-fatal assert.  Useful for debugging. */
#ifdef DEBUG
#define tu_assert(x)                                                         \
   ({                                                                        \
      if (unlikely(!(x)))                                                    \
         fprintf(stderr, "%s:%d ASSERT: %s\n", __FILE__, __LINE__, #x);      \
   })
#else
#define tu_assert(x)
#endif

/* Suppress -Wunused in stub functions */
#define tu_use_args(...) __tu_use_args(0, ##__VA_ARGS__)
static inline void
__tu_use_args(int ignore, ...)
{
}

#define tu_stub()                                                            \
   do {                                                                      \
      tu_finishme("stub %s", __func__);                                      \
   } while (0)

void *
tu_lookup_entrypoint_unchecked(const char *name);
void *
tu_lookup_entrypoint_checked(
   const char *name,
   uint32_t core_version,
   const struct tu_instance_extension_table *instance,
   const struct tu_device_extension_table *device);

struct tu_physical_device
{
   VK_LOADER_DATA _loader_data;

   struct tu_instance *instance;

   char path[20];
   char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t cache_uuid[VK_UUID_SIZE];

   struct wsi_device wsi_device;

   int local_fd;
   int master_fd;

   unsigned gpu_id;
   uint32_t gmem_size;
   uint32_t tile_align_w;
   uint32_t tile_align_h;

   /* This is the drivers on-disk cache used as a fallback as opposed to
    * the pipeline cache defined by apps.
    */
   struct disk_cache *disk_cache;

   struct tu_device_extension_table supported_extensions;
};

enum tu_debug_flags
{
   TU_DEBUG_STARTUP = 1 << 0,
   TU_DEBUG_NIR = 1 << 1,
   TU_DEBUG_IR3 = 1 << 2,
};

struct tu_instance
{
   VK_LOADER_DATA _loader_data;

   VkAllocationCallbacks alloc;

   uint32_t api_version;
   int physical_device_count;
   struct tu_physical_device physical_devices[TU_MAX_DRM_DEVICES];

   enum tu_debug_flags debug_flags;

   struct vk_debug_report_instance debug_report_callbacks;

   struct tu_instance_extension_table enabled_extensions;
};

VkResult
tu_wsi_init(struct tu_physical_device *physical_device);
void
tu_wsi_finish(struct tu_physical_device *physical_device);

bool
tu_instance_extension_supported(const char *name);
uint32_t
tu_physical_device_api_version(struct tu_physical_device *dev);
bool
tu_physical_device_extension_supported(struct tu_physical_device *dev,
                                       const char *name);

struct cache_entry;

struct tu_pipeline_cache
{
   struct tu_device *device;
   pthread_mutex_t mutex;

   uint32_t total_size;
   uint32_t table_size;
   uint32_t kernel_count;
   struct cache_entry **hash_table;
   bool modified;

   VkAllocationCallbacks alloc;
};

struct tu_pipeline_key
{
};

void
tu_pipeline_cache_init(struct tu_pipeline_cache *cache,
                       struct tu_device *device);
void
tu_pipeline_cache_finish(struct tu_pipeline_cache *cache);
void
tu_pipeline_cache_load(struct tu_pipeline_cache *cache,
                       const void *data,
                       size_t size);

struct tu_shader_variant;

bool
tu_create_shader_variants_from_pipeline_cache(
   struct tu_device *device,
   struct tu_pipeline_cache *cache,
   const unsigned char *sha1,
   struct tu_shader_variant **variants);

void
tu_pipeline_cache_insert_shaders(struct tu_device *device,
                                 struct tu_pipeline_cache *cache,
                                 const unsigned char *sha1,
                                 struct tu_shader_variant **variants,
                                 const void *const *codes,
                                 const unsigned *code_sizes);

struct tu_meta_state
{
   VkAllocationCallbacks alloc;

   struct tu_pipeline_cache cache;
};

/* queue types */
#define TU_QUEUE_GENERAL 0

#define TU_MAX_QUEUE_FAMILIES 1

struct tu_fence
{
   bool signaled;
   int fd;
};

void
tu_fence_init(struct tu_fence *fence, bool signaled);
void
tu_fence_finish(struct tu_fence *fence);
void
tu_fence_update_fd(struct tu_fence *fence, int fd);
void
tu_fence_copy(struct tu_fence *fence, const struct tu_fence *src);
void
tu_fence_signal(struct tu_fence *fence);
void
tu_fence_wait_idle(struct tu_fence *fence);

struct tu_queue
{
   VK_LOADER_DATA _loader_data;
   struct tu_device *device;
   uint32_t queue_family_index;
   int queue_idx;
   VkDeviceQueueCreateFlags flags;

   uint32_t msm_queue_id;
   struct tu_fence submit_fence;
};

struct tu_device
{
   VK_LOADER_DATA _loader_data;

   VkAllocationCallbacks alloc;

   struct tu_instance *instance;

   struct tu_meta_state meta_state;

   struct tu_queue *queues[TU_MAX_QUEUE_FAMILIES];
   int queue_count[TU_MAX_QUEUE_FAMILIES];

   struct tu_physical_device *physical_device;

   struct ir3_compiler *compiler;

   /* Backup in-memory cache to be used if the app doesn't provide one */
   struct tu_pipeline_cache *mem_cache;

   struct list_head shader_slabs;
   mtx_t shader_slab_mutex;

   struct tu_device_extension_table enabled_extensions;
};

struct tu_bo
{
   uint32_t gem_handle;
   uint64_t size;
   uint64_t iova;
   void *map;
};

VkResult
tu_bo_init_new(struct tu_device *dev, struct tu_bo *bo, uint64_t size);
VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo *bo,
                  uint64_t size,
                  int fd);
int
tu_bo_export_dmabuf(struct tu_device *dev, struct tu_bo *bo);
void
tu_bo_finish(struct tu_device *dev, struct tu_bo *bo);
VkResult
tu_bo_map(struct tu_device *dev, struct tu_bo *bo);

struct tu_cs_entry
{
   /* No ownership */
   const struct tu_bo *bo;

   uint32_t size;
   uint32_t offset;
};

enum tu_cs_mode
{

   /*
    * A command stream in TU_CS_MODE_GROW mode grows automatically whenever it
    * is full.  tu_cs_begin must be called before command packet emission and
    * tu_cs_end must be called after.
    *
    * This mode may create multiple entries internally.  The entries must be
    * submitted together.
    */
   TU_CS_MODE_GROW,

   /*
    * A command stream in TU_CS_MODE_EXTERNAL mode wraps an external,
    * fixed-size buffer.  tu_cs_begin and tu_cs_end are optional and have no
    * effect on it.
    *
    * This mode does not create any entry or any BO.
    */
   TU_CS_MODE_EXTERNAL,

   /*
    * A command stream in TU_CS_MODE_SUB_STREAM mode does not support direct
    * command packet emission.  tu_cs_begin_sub_stream must be called to get a
    * sub-stream to emit comamnd packets to.  When done with the sub-stream,
    * tu_cs_end_sub_stream must be called.
    *
    * This mode does not create any entry internally.
    */
   TU_CS_MODE_SUB_STREAM,
};

struct tu_cs
{
   uint32_t *start;
   uint32_t *cur;
   uint32_t *reserved_end;
   uint32_t *end;

   enum tu_cs_mode mode;
   uint32_t next_bo_size;

   struct tu_cs_entry *entries;
   uint32_t entry_count;
   uint32_t entry_capacity;

   struct tu_bo **bos;
   uint32_t bo_count;
   uint32_t bo_capacity;
};

struct tu_device_memory
{
   struct tu_bo bo;
   VkDeviceSize size;

   /* for dedicated allocations */
   struct tu_image *image;
   struct tu_buffer *buffer;

   uint32_t type_index;
   void *map;
   void *user_ptr;
};

struct tu_descriptor_range
{
   uint64_t va;
   uint32_t size;
};

struct tu_descriptor_set
{
   const struct tu_descriptor_set_layout *layout;
   uint32_t size;

   uint64_t va;
   uint32_t *mapped_ptr;
   struct tu_descriptor_range *dynamic_descriptors;
};

struct tu_push_descriptor_set
{
   struct tu_descriptor_set set;
   uint32_t capacity;
};

struct tu_descriptor_pool_entry
{
   uint32_t offset;
   uint32_t size;
   struct tu_descriptor_set *set;
};

struct tu_descriptor_pool
{
   uint8_t *mapped_ptr;
   uint64_t current_offset;
   uint64_t size;

   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;

   uint32_t entry_count;
   uint32_t max_entry_count;
   struct tu_descriptor_pool_entry entries[0];
};

struct tu_descriptor_update_template_entry
{
   VkDescriptorType descriptor_type;

   /* The number of descriptors to update */
   uint32_t descriptor_count;

   /* Into mapped_ptr or dynamic_descriptors, in units of the respective array
    */
   uint32_t dst_offset;

   /* In dwords. Not valid/used for dynamic descriptors */
   uint32_t dst_stride;

   uint32_t buffer_offset;

   /* Only valid for combined image samplers and samplers */
   uint16_t has_sampler;

   /* In bytes */
   size_t src_offset;
   size_t src_stride;

   /* For push descriptors */
   const uint32_t *immutable_samplers;
};

struct tu_descriptor_update_template
{
   uint32_t entry_count;
   VkPipelineBindPoint bind_point;
   struct tu_descriptor_update_template_entry entry[0];
};

struct tu_buffer
{
   VkDeviceSize size;

   VkBufferUsageFlags usage;
   VkBufferCreateFlags flags;

   struct tu_bo *bo;
   VkDeviceSize bo_offset;
};

enum tu_dynamic_state_bits
{
   TU_DYNAMIC_VIEWPORT = 1 << 0,
   TU_DYNAMIC_SCISSOR = 1 << 1,
   TU_DYNAMIC_LINE_WIDTH = 1 << 2,
   TU_DYNAMIC_DEPTH_BIAS = 1 << 3,
   TU_DYNAMIC_BLEND_CONSTANTS = 1 << 4,
   TU_DYNAMIC_DEPTH_BOUNDS = 1 << 5,
   TU_DYNAMIC_STENCIL_COMPARE_MASK = 1 << 6,
   TU_DYNAMIC_STENCIL_WRITE_MASK = 1 << 7,
   TU_DYNAMIC_STENCIL_REFERENCE = 1 << 8,
   TU_DYNAMIC_DISCARD_RECTANGLE = 1 << 9,
   TU_DYNAMIC_ALL = (1 << 10) - 1,
};

struct tu_vertex_binding
{
   struct tu_buffer *buffer;
   VkDeviceSize offset;
};

struct tu_viewport_state
{
   uint32_t count;
   VkViewport viewports[MAX_VIEWPORTS];
};

struct tu_scissor_state
{
   uint32_t count;
   VkRect2D scissors[MAX_SCISSORS];
};

struct tu_discard_rectangle_state
{
   uint32_t count;
   VkRect2D rectangles[MAX_DISCARD_RECTANGLES];
};

struct tu_dynamic_state
{
   /**
    * Bitmask of (1 << VK_DYNAMIC_STATE_*).
    * Defines the set of saved dynamic state.
    */
   uint32_t mask;

   struct tu_viewport_state viewport;

   struct tu_scissor_state scissor;

   float line_width;

   struct
   {
      float bias;
      float clamp;
      float slope;
   } depth_bias;

   float blend_constants[4];

   struct
   {
      float min;
      float max;
   } depth_bounds;

   struct
   {
      uint32_t front;
      uint32_t back;
   } stencil_compare_mask;

   struct
   {
      uint32_t front;
      uint32_t back;
   } stencil_write_mask;

   struct
   {
      uint32_t front;
      uint32_t back;
   } stencil_reference;

   struct tu_discard_rectangle_state discard_rectangle;
};

extern const struct tu_dynamic_state default_dynamic_state;

const char *
tu_get_debug_option_name(int id);

const char *
tu_get_perftest_option_name(int id);

/**
 * Attachment state when recording a renderpass instance.
 *
 * The clear value is valid only if there exists a pending clear.
 */
struct tu_attachment_state
{
   VkImageAspectFlags pending_clear_aspects;
   uint32_t cleared_views;
   VkClearValue clear_value;
   VkImageLayout current_layout;
};

struct tu_descriptor_state
{
   struct tu_descriptor_set *sets[MAX_SETS];
   uint32_t dirty;
   uint32_t valid;
   struct tu_push_descriptor_set push_set;
   bool push_dirty;
   uint32_t dynamic_buffers[4 * MAX_DYNAMIC_BUFFERS];
};

struct tu_tile
{
   uint8_t pipe;
   uint8_t slot;
   VkOffset2D begin;
   VkOffset2D end;
};

struct tu_tiling_config
{
   VkRect2D render_area;
   uint32_t buffer_cpp[MAX_RTS + 2];
   uint32_t buffer_count;

   /* position and size of the first tile */
   VkRect2D tile0;
   /* number of tiles */
   VkExtent2D tile_count;

   uint32_t gmem_offsets[MAX_RTS + 2];

   /* size of the first VSC pipe */
   VkExtent2D pipe0;
   /* number of VSC pipes */
   VkExtent2D pipe_count;

   /* pipe register values */
   uint32_t pipe_config[MAX_VSC_PIPES];
   uint32_t pipe_sizes[MAX_VSC_PIPES];
};

enum tu_cmd_dirty_bits
{
   TU_CMD_DIRTY_PIPELINE = 1 << 0,
   TU_CMD_DIRTY_VERTEX_BUFFERS = 1 << 1,

   TU_CMD_DIRTY_DYNAMIC_LINE_WIDTH = 1 << 16,
   TU_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK = 1 << 17,
   TU_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK = 1 << 18,
   TU_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE = 1 << 19,
};

struct tu_cmd_state
{
   uint32_t dirty;

   struct tu_pipeline *pipeline;

   /* Vertex buffers */
   struct
   {
      struct tu_buffer *buffers[MAX_VBS];
      VkDeviceSize offsets[MAX_VBS];
   } vb;

   struct tu_dynamic_state dynamic;

   /* Index buffer */
   struct tu_buffer *index_buffer;
   uint64_t index_offset;
   uint32_t index_type;
   uint32_t max_index_count;
   uint64_t index_va;

   const struct tu_render_pass *pass;
   const struct tu_subpass *subpass;
   const struct tu_framebuffer *framebuffer;
   struct tu_attachment_state *attachments;

   struct tu_tiling_config tiling_config;

   struct tu_cs_entry tile_load_ib;
   struct tu_cs_entry tile_store_ib;
};

struct tu_cmd_pool
{
   VkAllocationCallbacks alloc;
   struct list_head cmd_buffers;
   struct list_head free_cmd_buffers;
   uint32_t queue_family_index;
};

struct tu_cmd_buffer_upload
{
   uint8_t *map;
   unsigned offset;
   uint64_t size;
   struct list_head list;
};

enum tu_cmd_buffer_status
{
   TU_CMD_BUFFER_STATUS_INVALID,
   TU_CMD_BUFFER_STATUS_INITIAL,
   TU_CMD_BUFFER_STATUS_RECORDING,
   TU_CMD_BUFFER_STATUS_EXECUTABLE,
   TU_CMD_BUFFER_STATUS_PENDING,
};

struct tu_bo_list
{
   uint32_t count;
   uint32_t capacity;
   struct drm_msm_gem_submit_bo *bo_infos;
};

#define TU_BO_LIST_FAILED (~0)

void
tu_bo_list_init(struct tu_bo_list *list);
void
tu_bo_list_destroy(struct tu_bo_list *list);
void
tu_bo_list_reset(struct tu_bo_list *list);
uint32_t
tu_bo_list_add(struct tu_bo_list *list,
               const struct tu_bo *bo,
               uint32_t flags);
VkResult
tu_bo_list_merge(struct tu_bo_list *list, const struct tu_bo_list *other);

struct tu_cmd_buffer
{
   VK_LOADER_DATA _loader_data;

   struct tu_device *device;

   struct tu_cmd_pool *pool;
   struct list_head pool_link;

   VkCommandBufferUsageFlags usage_flags;
   VkCommandBufferLevel level;
   enum tu_cmd_buffer_status status;

   struct tu_cmd_state state;
   struct tu_vertex_binding vertex_bindings[MAX_VBS];
   uint32_t queue_family_index;

   uint8_t push_constants[MAX_PUSH_CONSTANTS_SIZE];
   VkShaderStageFlags push_constant_stages;
   struct tu_descriptor_set meta_push_descriptors;

   struct tu_descriptor_state descriptors[VK_PIPELINE_BIND_POINT_RANGE_SIZE];

   struct tu_cmd_buffer_upload upload;

   VkResult record_result;

   struct tu_bo_list bo_list;
   struct tu_cs cs;
   struct tu_cs draw_cs;
   struct tu_cs tile_cs;

   uint16_t marker_reg;
   uint32_t marker_seqno;

   struct tu_bo scratch_bo;
   uint32_t scratch_seqno;

   bool wait_for_idle;
};

void
tu6_emit_event_write(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     enum vgt_event_type event,
                     bool need_seqno);

bool
tu_get_memory_fd(struct tu_device *device,
                 struct tu_device_memory *memory,
                 int *pFD);

/*
 * Takes x,y,z as exact numbers of invocations, instead of blocks.
 *
 * Limitations: Can't call normal dispatch functions without binding or
 * rebinding
 *              the compute pipeline.
 */
void
tu_unaligned_dispatch(struct tu_cmd_buffer *cmd_buffer,
                      uint32_t x,
                      uint32_t y,
                      uint32_t z);

struct tu_event
{
   uint64_t *map;
};

struct tu_shader_module;

#define TU_HASH_SHADER_IS_GEOM_COPY_SHADER (1 << 0)
#define TU_HASH_SHADER_SISCHED (1 << 1)
#define TU_HASH_SHADER_UNSAFE_MATH (1 << 2)
void
tu_hash_shaders(unsigned char *hash,
                const VkPipelineShaderStageCreateInfo **stages,
                const struct tu_pipeline_layout *layout,
                const struct tu_pipeline_key *key,
                uint32_t flags);

static inline gl_shader_stage
vk_to_mesa_shader_stage(VkShaderStageFlagBits vk_stage)
{
   assert(__builtin_popcount(vk_stage) == 1);
   return ffs(vk_stage) - 1;
}

static inline VkShaderStageFlagBits
mesa_to_vk_shader_stage(gl_shader_stage mesa_stage)
{
   return (1 << mesa_stage);
}

#define TU_STAGE_MASK ((1 << MESA_SHADER_STAGES) - 1)

#define tu_foreach_stage(stage, stage_bits)                                  \
   for (gl_shader_stage stage,                                               \
        __tmp = (gl_shader_stage)((stage_bits) &TU_STAGE_MASK);              \
        stage = __builtin_ffs(__tmp) - 1, __tmp; __tmp &= ~(1 << (stage)))

struct tu_shader_module
{
   unsigned char sha1[20];

   uint32_t code_size;
   const uint32_t *code[0];
};

struct tu_shader_compile_options
{
   struct ir3_shader_key key;

   bool optimize;
   bool include_binning_pass;
};

struct tu_shader
{
   struct ir3_shader ir3_shader;

   /* This may be true for vertex shaders.  When true, variants[1] is the
    * binning variant and binning_binary is non-NULL.
    */
   bool has_binning_pass;

   void *binary;
   void *binning_binary;

   struct ir3_shader_variant variants[0];
};

struct tu_shader *
tu_shader_create(struct tu_device *dev,
                 gl_shader_stage stage,
                 const VkPipelineShaderStageCreateInfo *stage_info,
                 const VkAllocationCallbacks *alloc);

void
tu_shader_destroy(struct tu_device *dev,
                  struct tu_shader *shader,
                  const VkAllocationCallbacks *alloc);

void
tu_shader_compile_options_init(
   struct tu_shader_compile_options *options,
   const VkGraphicsPipelineCreateInfo *pipeline_info);

VkResult
tu_shader_compile(struct tu_device *dev,
                  struct tu_shader *shader,
                  const struct tu_shader *next_stage,
                  const struct tu_shader_compile_options *options,
                  const VkAllocationCallbacks *alloc);

struct tu_pipeline
{
   struct tu_cs cs;

   struct tu_dynamic_state dynamic_state;

   struct tu_pipeline_layout *layout;

   bool need_indirect_descriptor_sets;
   VkShaderStageFlags active_stages;

   struct
   {
      struct tu_bo binary_bo;
      struct tu_cs_entry state_ib;
      struct tu_cs_entry binning_state_ib;
   } program;

   struct
   {
      uint8_t bindings[MAX_VERTEX_ATTRIBS];
      uint16_t strides[MAX_VERTEX_ATTRIBS];
      uint16_t offsets[MAX_VERTEX_ATTRIBS];
      uint32_t count;

      uint8_t binning_bindings[MAX_VERTEX_ATTRIBS];
      uint16_t binning_strides[MAX_VERTEX_ATTRIBS];
      uint16_t binning_offsets[MAX_VERTEX_ATTRIBS];
      uint32_t binning_count;

      struct tu_cs_entry state_ib;
      struct tu_cs_entry binning_state_ib;
   } vi;

   struct
   {
      enum pc_di_primtype primtype;
      bool primitive_restart;
   } ia;

   struct
   {
      struct tu_cs_entry state_ib;
   } vp;

   struct
   {
      uint32_t gras_su_cntl;
      struct tu_cs_entry state_ib;
   } rast;

   struct
   {
      struct tu_cs_entry state_ib;
   } ds;

   struct
   {
      struct tu_cs_entry state_ib;
   } blend;
};

void
tu6_emit_viewport(struct tu_cs *cs, const VkViewport *viewport);

void
tu6_emit_scissor(struct tu_cs *cs, const VkRect2D *scissor);

void
tu6_emit_gras_su_cntl(struct tu_cs *cs,
                      uint32_t gras_su_cntl,
                      float line_width);

void
tu6_emit_depth_bias(struct tu_cs *cs,
                    float constant_factor,
                    float clamp,
                    float slope_factor);

void
tu6_emit_stencil_compare_mask(struct tu_cs *cs,
                              uint32_t front,
                              uint32_t back);

void
tu6_emit_stencil_write_mask(struct tu_cs *cs, uint32_t front, uint32_t back);

void
tu6_emit_stencil_reference(struct tu_cs *cs, uint32_t front, uint32_t back);

void
tu6_emit_blend_constants(struct tu_cs *cs, const float constants[4]);

struct tu_userdata_info *
tu_lookup_user_sgpr(struct tu_pipeline *pipeline,
                    gl_shader_stage stage,
                    int idx);

struct tu_shader_variant *
tu_get_shader(struct tu_pipeline *pipeline, gl_shader_stage stage);

struct tu_graphics_pipeline_create_info
{
   bool use_rectlist;
   bool db_depth_clear;
   bool db_stencil_clear;
   bool db_depth_disable_expclear;
   bool db_stencil_disable_expclear;
   bool db_flush_depth_inplace;
   bool db_flush_stencil_inplace;
   bool db_resummarize;
   uint32_t custom_blend_mode;
};

struct tu_native_format
{
   int vtx;      /* VFMTn_xxx or -1 */
   int tex;      /* TFMTn_xxx or -1 */
   int rb;       /* RBn_xxx or -1 */
   int swap;     /* enum a3xx_color_swap */
   bool present; /* internal only; always true to external users */
};

const struct tu_native_format *
tu6_get_native_format(VkFormat format);

int
tu_pack_clear_value(const VkClearValue *val,
                    VkFormat format,
                    uint32_t buf[4]);
enum a6xx_2d_ifmt tu6_rb_fmt_to_ifmt(enum a6xx_color_fmt fmt);

struct tu_image_level
{
   VkDeviceSize offset;
   VkDeviceSize size;
   uint32_t pitch;
};

struct tu_image
{
   VkImageType type;
   /* The original VkFormat provided by the client.  This may not match any
    * of the actual surface formats.
    */
   VkFormat vk_format;
   VkImageAspectFlags aspects;
   VkImageUsageFlags usage;  /**< Superset of VkImageCreateInfo::usage. */
   VkImageTiling tiling;     /** VkImageCreateInfo::tiling */
   VkImageCreateFlags flags; /** VkImageCreateInfo::flags */
   VkExtent3D extent;
   uint32_t level_count;
   uint32_t layer_count;

   VkDeviceSize size;
   uint32_t alignment;

   /* memory layout */
   VkDeviceSize layer_size;
   struct tu_image_level levels[15];
   unsigned tile_mode;

   unsigned queue_family_mask;
   bool exclusive;
   bool shareable;

   /* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
   VkDeviceMemory owned_memory;

   /* Set when bound */
   const struct tu_bo *bo;
   VkDeviceSize bo_offset;
};

unsigned
tu_image_queue_family_mask(const struct tu_image *image,
                           uint32_t family,
                           uint32_t queue_family);

static inline uint32_t
tu_get_layerCount(const struct tu_image *image,
                  const VkImageSubresourceRange *range)
{
   return range->layerCount == VK_REMAINING_ARRAY_LAYERS
             ? image->layer_count - range->baseArrayLayer
             : range->layerCount;
}

static inline uint32_t
tu_get_levelCount(const struct tu_image *image,
                  const VkImageSubresourceRange *range)
{
   return range->levelCount == VK_REMAINING_MIP_LEVELS
             ? image->level_count - range->baseMipLevel
             : range->levelCount;
}

struct tu_image_view
{
   struct tu_image *image; /**< VkImageViewCreateInfo::image */

   VkImageViewType type;
   VkImageAspectFlags aspect_mask;
   VkFormat vk_format;
   uint32_t base_layer;
   uint32_t layer_count;
   uint32_t base_mip;
   uint32_t level_count;
   VkExtent3D extent; /**< Extent of VkImageViewCreateInfo::baseMipLevel. */

   uint32_t descriptor[16];

   /* Descriptor for use as a storage image as opposed to a sampled image.
    * This has a few differences for cube maps (e.g. type).
    */
   uint32_t storage_descriptor[16];
};

struct tu_sampler
{
};

struct tu_image_create_info
{
   const VkImageCreateInfo *vk_info;
   bool scanout;
   bool no_metadata_planes;
};

VkResult
tu_image_create(VkDevice _device,
                const struct tu_image_create_info *info,
                const VkAllocationCallbacks *alloc,
                VkImage *pImage);

VkResult
tu_image_from_gralloc(VkDevice device_h,
                      const VkImageCreateInfo *base_info,
                      const VkNativeBufferANDROID *gralloc_info,
                      const VkAllocationCallbacks *alloc,
                      VkImage *out_image_h);

void
tu_image_view_init(struct tu_image_view *view,
                   struct tu_device *device,
                   const VkImageViewCreateInfo *pCreateInfo);

struct tu_buffer_view
{
   VkFormat vk_format;
   uint64_t range; /**< VkBufferViewCreateInfo::range */
   uint32_t state[4];
};
void
tu_buffer_view_init(struct tu_buffer_view *view,
                    struct tu_device *device,
                    const VkBufferViewCreateInfo *pCreateInfo);

static inline struct VkExtent3D
tu_sanitize_image_extent(const VkImageType imageType,
                         const struct VkExtent3D imageExtent)
{
   switch (imageType) {
   case VK_IMAGE_TYPE_1D:
      return (VkExtent3D) { imageExtent.width, 1, 1 };
   case VK_IMAGE_TYPE_2D:
      return (VkExtent3D) { imageExtent.width, imageExtent.height, 1 };
   case VK_IMAGE_TYPE_3D:
      return imageExtent;
   default:
      unreachable("invalid image type");
   }
}

static inline struct VkOffset3D
tu_sanitize_image_offset(const VkImageType imageType,
                         const struct VkOffset3D imageOffset)
{
   switch (imageType) {
   case VK_IMAGE_TYPE_1D:
      return (VkOffset3D) { imageOffset.x, 0, 0 };
   case VK_IMAGE_TYPE_2D:
      return (VkOffset3D) { imageOffset.x, imageOffset.y, 0 };
   case VK_IMAGE_TYPE_3D:
      return imageOffset;
   default:
      unreachable("invalid image type");
   }
}

struct tu_attachment_info
{
   struct tu_image_view *attachment;
};

struct tu_framebuffer
{
   uint32_t width;
   uint32_t height;
   uint32_t layers;

   uint32_t attachment_count;
   struct tu_attachment_info attachments[0];
};

struct tu_subpass_barrier
{
   VkPipelineStageFlags src_stage_mask;
   VkAccessFlags src_access_mask;
   VkAccessFlags dst_access_mask;
};

void
tu_subpass_barrier(struct tu_cmd_buffer *cmd_buffer,
                   const struct tu_subpass_barrier *barrier);

struct tu_subpass_attachment
{
   uint32_t attachment;
   VkImageLayout layout;
};

struct tu_subpass
{
   uint32_t input_count;
   uint32_t color_count;
   struct tu_subpass_attachment *input_attachments;
   struct tu_subpass_attachment *color_attachments;
   struct tu_subpass_attachment *resolve_attachments;
   struct tu_subpass_attachment depth_stencil_attachment;

   /** Subpass has at least one resolve attachment */
   bool has_resolve;

   struct tu_subpass_barrier start_barrier;

   uint32_t view_mask;
   VkSampleCountFlagBits max_sample_count;
};

struct tu_render_pass_attachment
{
   VkFormat format;
   uint32_t samples;
   VkAttachmentLoadOp load_op;
   VkAttachmentLoadOp stencil_load_op;
   VkImageLayout initial_layout;
   VkImageLayout final_layout;
   uint32_t view_mask;
};

struct tu_render_pass
{
   uint32_t attachment_count;
   uint32_t subpass_count;
   struct tu_subpass_attachment *subpass_attachments;
   struct tu_render_pass_attachment *attachments;
   struct tu_subpass_barrier end_barrier;
   struct tu_subpass subpasses[0];
};

VkResult
tu_device_init_meta(struct tu_device *device);
void
tu_device_finish_meta(struct tu_device *device);

struct tu_query_pool
{
   uint32_t stride;
   uint32_t availability_offset;
   uint64_t size;
   char *ptr;
   VkQueryType type;
   uint32_t pipeline_stats_mask;
};

struct tu_semaphore
{
   uint32_t syncobj;
   uint32_t temp_syncobj;
};

void
tu_set_descriptor_set(struct tu_cmd_buffer *cmd_buffer,
                      VkPipelineBindPoint bind_point,
                      struct tu_descriptor_set *set,
                      unsigned idx);

void
tu_update_descriptor_sets(struct tu_device *device,
                          struct tu_cmd_buffer *cmd_buffer,
                          VkDescriptorSet overrideSet,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies);

void
tu_update_descriptor_set_with_template(
   struct tu_device *device,
   struct tu_cmd_buffer *cmd_buffer,
   struct tu_descriptor_set *set,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData);

void
tu_meta_push_descriptor_set(struct tu_cmd_buffer *cmd_buffer,
                            VkPipelineBindPoint pipelineBindPoint,
                            VkPipelineLayout _layout,
                            uint32_t set,
                            uint32_t descriptorWriteCount,
                            const VkWriteDescriptorSet *pDescriptorWrites);

int
tu_drm_get_gpu_id(const struct tu_physical_device *dev, uint32_t *id);

int
tu_drm_get_gmem_size(const struct tu_physical_device *dev, uint32_t *size);

int
tu_drm_submitqueue_new(const struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id);

void
tu_drm_submitqueue_close(const struct tu_device *dev, uint32_t queue_id);

uint32_t
tu_gem_new(const struct tu_device *dev, uint64_t size, uint32_t flags);
uint32_t
tu_gem_import_dmabuf(const struct tu_device *dev,
                     int prime_fd,
                     uint64_t size);
int
tu_gem_export_dmabuf(const struct tu_device *dev, uint32_t gem_handle);
void
tu_gem_close(const struct tu_device *dev, uint32_t gem_handle);
uint64_t
tu_gem_info_offset(const struct tu_device *dev, uint32_t gem_handle);
uint64_t
tu_gem_info_iova(const struct tu_device *dev, uint32_t gem_handle);

#define TU_DEFINE_HANDLE_CASTS(__tu_type, __VkType)                          \
                                                                             \
   static inline struct __tu_type *__tu_type##_from_handle(__VkType _handle) \
   {                                                                         \
      return (struct __tu_type *) _handle;                                   \
   }                                                                         \
                                                                             \
   static inline __VkType __tu_type##_to_handle(struct __tu_type *_obj)      \
   {                                                                         \
      return (__VkType) _obj;                                                \
   }

#define TU_DEFINE_NONDISP_HANDLE_CASTS(__tu_type, __VkType)                  \
                                                                             \
   static inline struct __tu_type *__tu_type##_from_handle(__VkType _handle) \
   {                                                                         \
      return (struct __tu_type *) (uintptr_t) _handle;                       \
   }                                                                         \
                                                                             \
   static inline __VkType __tu_type##_to_handle(struct __tu_type *_obj)      \
   {                                                                         \
      return (__VkType)(uintptr_t) _obj;                                     \
   }

#define TU_FROM_HANDLE(__tu_type, __name, __handle)                          \
   struct __tu_type *__name = __tu_type##_from_handle(__handle)

TU_DEFINE_HANDLE_CASTS(tu_cmd_buffer, VkCommandBuffer)
TU_DEFINE_HANDLE_CASTS(tu_device, VkDevice)
TU_DEFINE_HANDLE_CASTS(tu_instance, VkInstance)
TU_DEFINE_HANDLE_CASTS(tu_physical_device, VkPhysicalDevice)
TU_DEFINE_HANDLE_CASTS(tu_queue, VkQueue)

TU_DEFINE_NONDISP_HANDLE_CASTS(tu_cmd_pool, VkCommandPool)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer, VkBuffer)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer_view, VkBufferView)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_pool, VkDescriptorPool)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set, VkDescriptorSet)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set_layout,
                               VkDescriptorSetLayout)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_update_template,
                               VkDescriptorUpdateTemplate)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_device_memory, VkDeviceMemory)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_fence, VkFence)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_event, VkEvent)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_framebuffer, VkFramebuffer)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_image, VkImage)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_image_view, VkImageView);
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_cache, VkPipelineCache)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline, VkPipeline)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_layout, VkPipelineLayout)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_query_pool, VkQueryPool)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_render_pass, VkRenderPass)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_sampler, VkSampler)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_shader_module, VkShaderModule)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_semaphore, VkSemaphore)

#endif /* TU_PRIVATE_H */
