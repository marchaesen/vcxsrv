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
#define VG(x) ((void)0)
#endif

#define MESA_LOG_TAG "TU"

#include "c11/threads.h"
#include "util/rounding.h"
#include "util/bitscan.h"
#include "util/list.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_atomic.h"
#include "util/u_dynarray.h"
#include "util/xmlconfig.h"
#include "util/perf/u_trace.h"
#include "vk_alloc.h"
#include "vk_debug_report.h"
#include "vk_device.h"
#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_instance.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_shader_module.h"
#include "wsi_common.h"

#include "ir3/ir3_compiler.h"
#include "ir3/ir3_shader.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"
#include "a6xx.xml.h"
#include "fdl/freedreno_layout.h"
#include "common/freedreno_dev_info.h"
#include "perfcntrs/freedreno_perfcntr.h"

#include "tu_descriptor_set.h"
#include "tu_autotune.h"
#include "tu_util.h"
#include "tu_perfetto.h"

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>

#include "tu_entrypoints.h"

#include "vk_format.h"
#include "vk_image.h"
#include "vk_command_buffer.h"
#include "vk_queue.h"
#include "vk_object.h"
#include "vk_sync.h"
#include "vk_fence.h"
#include "vk_semaphore.h"
#include "vk_drm_syncobj.h"
#include "vk_sync_timeline.h"

#define MAX_VBS 32
#define MAX_VERTEX_ATTRIBS 32
#define MAX_RTS 8
#define MAX_VSC_PIPES 32
#define MAX_VIEWPORTS 16
#define MAX_VIEWPORT_SIZE (1 << 14)
#define MAX_SCISSORS 16
#define MAX_DISCARD_RECTANGLES 4
#define MAX_PUSH_CONSTANTS_SIZE 128
#define MAX_PUSH_DESCRIPTORS 32
#define MAX_DYNAMIC_UNIFORM_BUFFERS 16
#define MAX_DYNAMIC_STORAGE_BUFFERS 8
#define MAX_DYNAMIC_BUFFERS                                                  \
   (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)
#define TU_MAX_DRM_DEVICES 8
#define MAX_VIEWS 16
#define MAX_BIND_POINTS 2 /* compute + graphics */
/* The Qualcomm driver exposes 0x20000058 */
#define MAX_STORAGE_BUFFER_RANGE 0x20000000
/* We use ldc for uniform buffer loads, just like the Qualcomm driver, so
 * expose the same maximum range.
 * TODO: The SIZE bitfield is 15 bits, and in 4-dword units, so the actual
 * range might be higher.
 */
#define MAX_UNIFORM_BUFFER_RANGE 0x10000

#define A6XX_TEX_CONST_DWORDS 16
#define A6XX_TEX_SAMP_DWORDS 4

#define COND(bool, val) ((bool) ? (val) : 0)
#define BIT(bit) (1u << (bit))

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

struct tu_instance;

VkResult
__vk_startup_errorf(struct tu_instance *instance,
                    VkResult error,
                    bool force_print,
                    const char *file,
                    int line,
                    const char *format,
                    ...) PRINTFLIKE(6, 7);

/* Prints startup errors if TU_DEBUG=startup is set or on a debug driver
 * build.
 */
#define vk_startup_errorf(instance, error, format, ...) \
   __vk_startup_errorf(instance, error, \
                       instance->debug_flags & TU_DEBUG_STARTUP, \
                       __FILE__, __LINE__, format, ##__VA_ARGS__)

void
__tu_finishme(const char *file, int line, const char *format, ...)
   PRINTFLIKE(3, 4);

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

#define tu_stub()                                                            \
   do {                                                                      \
      tu_finishme("stub %s", __func__);                                      \
   } while (0)

struct tu_memory_heap {
   /* Standard bits passed on to the client */
   VkDeviceSize      size;
   VkMemoryHeapFlags flags;

   /** Copied from ANV:
    *
    * Driver-internal book-keeping.
    *
    * Align it to 64 bits to make atomic operations faster on 32 bit platforms.
    */
   VkDeviceSize      used __attribute__ ((aligned (8)));
};

uint64_t
tu_get_system_heap_size(void);

struct tu_physical_device
{
   struct vk_physical_device vk;

   struct tu_instance *instance;

   const char *name;
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t cache_uuid[VK_UUID_SIZE];

   struct wsi_device wsi_device;

   int local_fd;
   int master_fd;

   uint32_t gmem_size;
   uint64_t gmem_base;
   uint32_t ccu_offset_gmem;
   uint32_t ccu_offset_bypass;

   struct fd_dev_id dev_id;
   const struct fd_dev_info *info;

   int msm_major_version;
   int msm_minor_version;

   /* This is the drivers on-disk cache used as a fallback as opposed to
    * the pipeline cache defined by apps.
    */
   struct disk_cache *disk_cache;

   struct tu_memory_heap heap;

   struct vk_sync_type syncobj_type;
   struct vk_sync_timeline_type timeline_type;
   const struct vk_sync_type *sync_types[3];
};

enum tu_debug_flags
{
   TU_DEBUG_STARTUP = 1 << 0,
   TU_DEBUG_NIR = 1 << 1,
   TU_DEBUG_NOBIN = 1 << 3,
   TU_DEBUG_SYSMEM = 1 << 4,
   TU_DEBUG_FORCEBIN = 1 << 5,
   TU_DEBUG_NOUBWC = 1 << 6,
   TU_DEBUG_NOMULTIPOS = 1 << 7,
   TU_DEBUG_NOLRZ = 1 << 8,
   TU_DEBUG_PERFC = 1 << 9,
   TU_DEBUG_FLUSHALL = 1 << 10,
   TU_DEBUG_SYNCDRAW = 1 << 11,
   TU_DEBUG_DONT_CARE_AS_LOAD = 1 << 12,
   TU_DEBUG_GMEM = 1 << 13,
};

struct tu_instance
{
   struct vk_instance vk;

   uint32_t api_version;
   int physical_device_count;
   struct tu_physical_device physical_devices[TU_MAX_DRM_DEVICES];

   struct driOptionCache dri_options;
   struct driOptionCache available_dri_options;

   enum tu_debug_flags debug_flags;
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
   struct vk_object_base base;

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


/* queue types */
#define TU_QUEUE_GENERAL 0

#define TU_MAX_QUEUE_FAMILIES 1

/* Keep tu_syncobj until porting to common code for kgsl too */
#ifdef TU_USE_KGSL
struct tu_syncobj;
#endif
struct tu_u_trace_syncobj;

/* Define tu_timeline_sync type based on drm syncobj for a point type
 * for vk_sync_timeline, and the logic to handle is mostly copied from
 * anv_bo_sync since it seems it can be used by similar way to anv.
 */
enum tu_timeline_sync_state {
   /** Indicates that this is a new (or newly reset fence) */
   TU_TIMELINE_SYNC_STATE_RESET,

   /** Indicates that this fence has been submitted to the GPU but is still
    * (as far as we know) in use by the GPU.
    */
   TU_TIMELINE_SYNC_STATE_SUBMITTED,

   TU_TIMELINE_SYNC_STATE_SIGNALED,
};

struct tu_timeline_sync {
   struct vk_sync base;

   enum tu_timeline_sync_state state;
   uint32_t syncobj;
};

struct tu_queue
{
   struct vk_queue vk;

   struct tu_device *device;

   uint32_t msm_queue_id;
   int fence;
};

struct tu_bo
{
   uint32_t gem_handle;
   uint64_t size;
   uint64_t iova;
   void *map;
};

enum global_shader {
   GLOBAL_SH_VS_BLIT,
   GLOBAL_SH_VS_CLEAR,
   GLOBAL_SH_FS_BLIT,
   GLOBAL_SH_FS_BLIT_ZSCALE,
   GLOBAL_SH_FS_COPY_MS,
   GLOBAL_SH_FS_CLEAR0,
   GLOBAL_SH_FS_CLEAR_MAX = GLOBAL_SH_FS_CLEAR0 + MAX_RTS,
   GLOBAL_SH_COUNT,
};

#define TU_BORDER_COLOR_COUNT 4096
#define TU_BORDER_COLOR_BUILTIN 6

#define TU_BLIT_SHADER_SIZE 1024

/* This struct defines the layout of the global_bo */
struct tu6_global
{
   /* clear/blit shaders */
   uint32_t shaders[TU_BLIT_SHADER_SIZE];

   uint32_t seqno_dummy;          /* dummy seqno for CP_EVENT_WRITE */
   uint32_t _pad0;
   volatile uint32_t vsc_draw_overflow;
   uint32_t _pad1;
   volatile uint32_t vsc_prim_overflow;
   uint32_t _pad2;
   uint64_t predicate;

   /* scratch space for VPC_SO[i].FLUSH_BASE_LO/HI, start on 32 byte boundary. */
   struct {
      uint32_t offset;
      uint32_t pad[7];
   } flush_base[4];

   ALIGN16 uint32_t cs_indirect_xyz[3];

   /* note: larger global bo will be used for customBorderColors */
   struct bcolor_entry bcolor_builtin[TU_BORDER_COLOR_BUILTIN], bcolor[];
};
#define gb_offset(member) offsetof(struct tu6_global, member)
#define global_iova(cmd, member) ((cmd)->device->global_bo.iova + gb_offset(member))

/* extra space in vsc draw/prim streams */
#define VSC_PAD 0x40

struct tu_device
{
   struct vk_device vk;
   struct tu_instance *instance;

   struct tu_queue *queues[TU_MAX_QUEUE_FAMILIES];
   int queue_count[TU_MAX_QUEUE_FAMILIES];

   struct tu_physical_device *physical_device;
   int fd;

   struct ir3_compiler *compiler;

   /* Backup in-memory cache to be used if the app doesn't provide one */
   struct tu_pipeline_cache *mem_cache;

#define MIN_SCRATCH_BO_SIZE_LOG2 12 /* A page */

   /* Currently the kernel driver uses a 32-bit GPU address space, but it
    * should be impossible to go beyond 48 bits.
    */
   struct {
      struct tu_bo bo;
      mtx_t construct_mtx;
      bool initialized;
   } scratch_bos[48 - MIN_SCRATCH_BO_SIZE_LOG2];

   struct tu_bo global_bo;

   /* the blob seems to always use 8K factor and 128K param sizes, copy them */
#define TU_TESS_FACTOR_SIZE (8 * 1024)
#define TU_TESS_PARAM_SIZE (128 * 1024)
#define TU_TESS_BO_SIZE (TU_TESS_FACTOR_SIZE + TU_TESS_PARAM_SIZE)
   /* Lazily allocated, protected by the device mutex. */
   struct tu_bo tess_bo;

   struct ir3_shader_variant *global_shaders[GLOBAL_SH_COUNT];
   uint64_t global_shader_va[GLOBAL_SH_COUNT];

   uint32_t vsc_draw_strm_pitch;
   uint32_t vsc_prim_strm_pitch;
   BITSET_DECLARE(custom_border_color, TU_BORDER_COLOR_COUNT);
   mtx_t mutex;

   /* bo list for submits: */
   struct drm_msm_gem_submit_bo *bo_list;
   /* map bo handles to bo list index: */
   uint32_t *bo_idx;
   uint32_t bo_count, bo_list_size, bo_idx_size;
   mtx_t bo_mutex;

   /* Command streams to set pass index to a scratch reg */
   struct tu_cs *perfcntrs_pass_cs;
   struct tu_cs_entry *perfcntrs_pass_cs_entries;

   /* Condition variable for timeline semaphore to notify waiters when a
    * new submit is executed. */
   pthread_cond_t timeline_cond;
   pthread_mutex_t submit_mutex;

   struct tu_autotune autotune;

#ifdef ANDROID
   const void *gralloc;
   enum {
      TU_GRALLOC_UNKNOWN,
      TU_GRALLOC_CROS,
      TU_GRALLOC_OTHER,
   } gralloc_type;
#endif

   uint32_t submit_count;

   struct u_trace_context trace_context;

   #ifdef HAVE_PERFETTO
   struct tu_perfetto_state perfetto;
   #endif
};

void tu_init_clear_blit_shaders(struct tu_device *dev);

void tu_destroy_clear_blit_shaders(struct tu_device *dev);

VkResult
tu_device_submit_deferred_locked(struct tu_device *dev);

VkResult
tu_device_wait_u_trace(struct tu_device *dev, struct tu_u_trace_syncobj *syncobj);

uint64_t
tu_device_ticks_to_ns(struct tu_device *dev, uint64_t ts);

enum tu_bo_alloc_flags
{
   TU_BO_ALLOC_NO_FLAGS = 0,
   TU_BO_ALLOC_ALLOW_DUMP = 1 << 0,
   TU_BO_ALLOC_GPU_READ_ONLY = 1 << 1,
};

VkResult
tu_bo_init_new(struct tu_device *dev, struct tu_bo *bo, uint64_t size,
               enum tu_bo_alloc_flags flags);
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

/* Get a scratch bo for use inside a command buffer. This will always return
 * the same bo given the same size or similar sizes, so only one scratch bo
 * can be used at the same time. It's meant for short-lived things where we
 * need to write to some piece of memory, read from it, and then immediately
 * discard it.
 */
VkResult
tu_get_scratch_bo(struct tu_device *dev, uint64_t size, struct tu_bo **bo);

struct tu_cs_entry
{
   /* No ownership */
   const struct tu_bo *bo;

   uint32_t size;
   uint32_t offset;
};

struct tu_cs_memory {
   uint32_t *map;
   uint64_t iova;
};

struct tu_draw_state {
   uint64_t iova : 48;
   uint32_t size : 16;
};

enum tu_dynamic_state
{
   /* re-use VK_DYNAMIC_STATE_ enums for non-extended dynamic states */
   TU_DYNAMIC_STATE_SAMPLE_LOCATIONS = VK_DYNAMIC_STATE_STENCIL_REFERENCE + 1,
   TU_DYNAMIC_STATE_RB_DEPTH_CNTL,
   TU_DYNAMIC_STATE_RB_STENCIL_CNTL,
   TU_DYNAMIC_STATE_VB_STRIDE,
   TU_DYNAMIC_STATE_RASTERIZER_DISCARD,
   TU_DYNAMIC_STATE_COUNT,
   /* no associated draw state: */
   TU_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY = TU_DYNAMIC_STATE_COUNT,
   TU_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
   /* re-use the line width enum as it uses GRAS_SU_CNTL: */
   TU_DYNAMIC_STATE_GRAS_SU_CNTL = VK_DYNAMIC_STATE_LINE_WIDTH,
};

enum tu_draw_state_group_id
{
   TU_DRAW_STATE_PROGRAM_CONFIG,
   TU_DRAW_STATE_PROGRAM,
   TU_DRAW_STATE_PROGRAM_BINNING,
   TU_DRAW_STATE_VB,
   TU_DRAW_STATE_VI,
   TU_DRAW_STATE_VI_BINNING,
   TU_DRAW_STATE_RAST,
   TU_DRAW_STATE_BLEND,
   TU_DRAW_STATE_SHADER_GEOM_CONST,
   TU_DRAW_STATE_FS_CONST,
   TU_DRAW_STATE_DESC_SETS,
   TU_DRAW_STATE_DESC_SETS_LOAD,
   TU_DRAW_STATE_VS_PARAMS,
   TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM,
   TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM,
   TU_DRAW_STATE_LRZ,
   TU_DRAW_STATE_DEPTH_PLANE,

   /* dynamic state related draw states */
   TU_DRAW_STATE_DYNAMIC,
   TU_DRAW_STATE_COUNT = TU_DRAW_STATE_DYNAMIC + TU_DYNAMIC_STATE_COUNT,
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

   struct tu_device *device;
   enum tu_cs_mode mode;
   uint32_t next_bo_size;

   struct tu_cs_entry *entries;
   uint32_t entry_count;
   uint32_t entry_capacity;

   struct tu_bo **bos;
   uint32_t bo_count;
   uint32_t bo_capacity;

   /* state for cond_exec_start/cond_exec_end */
   uint32_t cond_flags;
   uint32_t *cond_dwords;
};

struct tu_device_memory
{
   struct vk_object_base base;

   struct tu_bo bo;
};

struct tu_descriptor_range
{
   uint64_t va;
   uint32_t size;
};

struct tu_descriptor_set
{
   struct vk_object_base base;

   /* Link to descriptor pool's desc_sets list . */
   struct list_head pool_link;

   struct tu_descriptor_set_layout *layout;
   struct tu_descriptor_pool *pool;
   uint32_t size;

   uint64_t va;
   uint32_t *mapped_ptr;

   uint32_t *dynamic_descriptors;
};

struct tu_descriptor_pool_entry
{
   uint32_t offset;
   uint32_t size;
   struct tu_descriptor_set *set;
};

struct tu_descriptor_pool
{
   struct vk_object_base base;

   struct tu_bo bo;
   uint64_t current_offset;
   uint64_t size;

   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;
   uint8_t *host_bo;

   struct list_head desc_sets;

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
   const struct tu_sampler *immutable_samplers;
};

struct tu_descriptor_update_template
{
   struct vk_object_base base;

   uint32_t entry_count;
   VkPipelineBindPoint bind_point;
   struct tu_descriptor_update_template_entry entry[0];
};

struct tu_buffer
{
   struct vk_object_base base;

   VkDeviceSize size;

   VkBufferUsageFlags usage;
   VkBufferCreateFlags flags;

   struct tu_bo *bo;
   uint64_t iova;
};

const char *
tu_get_debug_option_name(int id);

const char *
tu_get_perftest_option_name(int id);

struct tu_descriptor_state
{
   struct tu_descriptor_set *sets[MAX_SETS];
   struct tu_descriptor_set push_set;
   uint32_t dynamic_descriptors[MAX_DYNAMIC_BUFFERS * A6XX_TEX_CONST_DWORDS];
};

enum tu_cmd_dirty_bits
{
   TU_CMD_DIRTY_VERTEX_BUFFERS = BIT(0),
   TU_CMD_DIRTY_VB_STRIDE = BIT(1),
   TU_CMD_DIRTY_GRAS_SU_CNTL = BIT(2),
   TU_CMD_DIRTY_RB_DEPTH_CNTL = BIT(3),
   TU_CMD_DIRTY_RB_STENCIL_CNTL = BIT(4),
   TU_CMD_DIRTY_DESC_SETS_LOAD = BIT(5),
   TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD = BIT(6),
   TU_CMD_DIRTY_SHADER_CONSTS = BIT(7),
   TU_CMD_DIRTY_LRZ = BIT(8),
   TU_CMD_DIRTY_VS_PARAMS = BIT(9),
   TU_CMD_DIRTY_RASTERIZER_DISCARD = BIT(10),
   /* all draw states were disabled and need to be re-enabled: */
   TU_CMD_DIRTY_DRAW_STATE = BIT(11)
};

/* There are only three cache domains we have to care about: the CCU, or
 * color cache unit, which is used for color and depth/stencil attachments
 * and copy/blit destinations, and is split conceptually into color and depth,
 * and the universal cache or UCHE which is used for pretty much everything
 * else, except for the CP (uncached) and host. We need to flush whenever data
 * crosses these boundaries.
 */

enum tu_cmd_access_mask {
   TU_ACCESS_UCHE_READ = 1 << 0,
   TU_ACCESS_UCHE_WRITE = 1 << 1,
   TU_ACCESS_CCU_COLOR_READ = 1 << 2,
   TU_ACCESS_CCU_COLOR_WRITE = 1 << 3,
   TU_ACCESS_CCU_DEPTH_READ = 1 << 4,
   TU_ACCESS_CCU_DEPTH_WRITE = 1 << 5,

   /* Experiments have shown that while it's safe to avoid flushing the CCU
    * after each blit/renderpass, it's not safe to assume that subsequent
    * lookups with a different attachment state will hit unflushed cache
    * entries. That is, the CCU needs to be flushed and possibly invalidated
    * when accessing memory with a different attachment state. Writing to an
    * attachment under the following conditions after clearing using the
    * normal 2d engine path is known to have issues:
    *
    * - It isn't the 0'th layer.
    * - There are more than one attachment, and this isn't the 0'th attachment
    *   (this seems to also depend on the cpp of the attachments).
    *
    * Our best guess is that the layer/MRT state is used when computing
    * the location of a cache entry in CCU, to avoid conflicts. We assume that
    * any access in a renderpass after or before an access by a transfer needs
    * a flush/invalidate, and use the _INCOHERENT variants to represent access
    * by a renderpass.
    */
   TU_ACCESS_CCU_COLOR_INCOHERENT_READ = 1 << 6,
   TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE = 1 << 7,
   TU_ACCESS_CCU_DEPTH_INCOHERENT_READ = 1 << 8,
   TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE = 1 << 9,

   /* Accesses which bypasses any cache. e.g. writes via the host,
    * CP_EVENT_WRITE::BLIT, and the CP are SYSMEM_WRITE.
    */
   TU_ACCESS_SYSMEM_READ = 1 << 10,
   TU_ACCESS_SYSMEM_WRITE = 1 << 11,

   /* Memory writes from the CP start in-order with draws and event writes,
    * but execute asynchronously and hence need a CP_WAIT_MEM_WRITES if read.
    */
   TU_ACCESS_CP_WRITE = 1 << 12,

   TU_ACCESS_READ =
      TU_ACCESS_UCHE_READ |
      TU_ACCESS_CCU_COLOR_READ |
      TU_ACCESS_CCU_DEPTH_READ |
      TU_ACCESS_CCU_COLOR_INCOHERENT_READ |
      TU_ACCESS_CCU_DEPTH_INCOHERENT_READ |
      TU_ACCESS_SYSMEM_READ,

   TU_ACCESS_WRITE =
      TU_ACCESS_UCHE_WRITE |
      TU_ACCESS_CCU_COLOR_WRITE |
      TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE |
      TU_ACCESS_CCU_DEPTH_WRITE |
      TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE |
      TU_ACCESS_SYSMEM_WRITE |
      TU_ACCESS_CP_WRITE,

   TU_ACCESS_ALL =
      TU_ACCESS_READ |
      TU_ACCESS_WRITE,
};

/* Starting with a6xx, the pipeline is split into several "clusters" (really
 * pipeline stages). Each stage has its own pair of register banks and can
 * switch them independently, so that earlier stages can run ahead of later
 * ones. e.g. the FS of draw N and the VS of draw N + 1 can be executing at
 * the same time.
 *
 * As a result of this, we need to insert a WFI when an earlier stage depends
 * on the result of a later stage. CP_DRAW_* and CP_BLIT will wait for any
 * pending WFI's to complete before starting, and usually before reading
 * indirect params even, so a WFI also acts as a full "pipeline stall".
 *
 * Note, the names of the stages come from CLUSTER_* in devcoredump. We
 * include all the stages for completeness, even ones which do not read/write
 * anything.
 */

enum tu_stage {
   /* This doesn't correspond to a cluster, but we need it for tracking
    * indirect draw parameter reads etc.
    */
   TU_STAGE_CP,

   /* - Fetch index buffer
    * - Fetch vertex attributes, dispatch VS
    */
   TU_STAGE_FE,

   /* Execute all geometry stages (VS thru GS) */
   TU_STAGE_SP_VS,

   /* Write to VPC, do primitive assembly. */
   TU_STAGE_PC_VS,

   /* Rasterization. RB_DEPTH_BUFFER_BASE only exists in CLUSTER_PS according
    * to devcoredump so presumably this stage stalls for TU_STAGE_PS when
    * early depth testing is enabled before dispatching fragments? However
    * GRAS reads and writes LRZ directly.
    */
   TU_STAGE_GRAS,

   /* Execute FS */
   TU_STAGE_SP_PS,

   /* - Fragment tests
    * - Write color/depth
    * - Streamout writes (???)
    * - Varying interpolation (???)
    */
   TU_STAGE_PS,
};

enum tu_cmd_flush_bits {
   TU_CMD_FLAG_CCU_FLUSH_DEPTH = 1 << 0,
   TU_CMD_FLAG_CCU_FLUSH_COLOR = 1 << 1,
   TU_CMD_FLAG_CCU_INVALIDATE_DEPTH = 1 << 2,
   TU_CMD_FLAG_CCU_INVALIDATE_COLOR = 1 << 3,
   TU_CMD_FLAG_CACHE_FLUSH = 1 << 4,
   TU_CMD_FLAG_CACHE_INVALIDATE = 1 << 5,
   TU_CMD_FLAG_WAIT_MEM_WRITES = 1 << 6,
   TU_CMD_FLAG_WAIT_FOR_IDLE = 1 << 7,
   TU_CMD_FLAG_WAIT_FOR_ME = 1 << 8,

   TU_CMD_FLAG_ALL_FLUSH =
      TU_CMD_FLAG_CCU_FLUSH_DEPTH |
      TU_CMD_FLAG_CCU_FLUSH_COLOR |
      TU_CMD_FLAG_CACHE_FLUSH |
      /* Treat the CP as a sort of "cache" which may need to be "flushed" via
       * waiting for writes to land with WAIT_FOR_MEM_WRITES.
       */
      TU_CMD_FLAG_WAIT_MEM_WRITES,

   TU_CMD_FLAG_ALL_INVALIDATE =
      TU_CMD_FLAG_CCU_INVALIDATE_DEPTH |
      TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
      TU_CMD_FLAG_CACHE_INVALIDATE,
};

/* Changing the CCU from sysmem mode to gmem mode or vice-versa is pretty
 * heavy, involving a CCU cache flush/invalidate and a WFI in order to change
 * which part of the gmem is used by the CCU. Here we keep track of what the
 * state of the CCU.
 */
enum tu_cmd_ccu_state {
   TU_CMD_CCU_SYSMEM,
   TU_CMD_CCU_GMEM,
   TU_CMD_CCU_UNKNOWN,
};

struct tu_cache_state {
   /* Caches which must be made available (flushed) eventually if there are
    * any users outside that cache domain, and caches which must be
    * invalidated eventually if there are any reads.
    */
   enum tu_cmd_flush_bits pending_flush_bits;
   /* Pending flushes */
   enum tu_cmd_flush_bits flush_bits;
};

enum tu_lrz_force_disable_mask {
   TU_LRZ_FORCE_DISABLE_LRZ = 1 << 0,
   TU_LRZ_FORCE_DISABLE_WRITE = 1 << 1,
};

enum tu_lrz_direction {
   TU_LRZ_UNKNOWN,
   /* Depth func less/less-than: */
   TU_LRZ_LESS,
   /* Depth func greater/greater-than: */
   TU_LRZ_GREATER,
};

struct tu_lrz_pipeline
{
   uint32_t force_disable_mask;
   bool fs_has_kill;
   bool force_late_z;
   bool early_fragment_tests;
};

struct tu_lrz_state
{
   /* Depth/Stencil image currently on use to do LRZ */
   struct tu_image *image;
   bool valid : 1;
   struct tu_draw_state state;
   enum tu_lrz_direction prev_direction;
};

struct tu_vs_params {
   uint32_t vertex_offset;
   uint32_t first_instance;
};

struct tu_cmd_state
{
   uint32_t dirty;

   struct tu_pipeline *pipeline;
   struct tu_pipeline *compute_pipeline;

   /* Vertex buffers, viewports, and scissors
    * the states for these can be updated partially, so we need to save these
    * to be able to emit a complete draw state
    */
   struct {
      uint64_t base;
      uint32_t size;
      uint32_t stride;
   } vb[MAX_VBS];
   VkViewport viewport[MAX_VIEWPORTS];
   VkRect2D scissor[MAX_SCISSORS];
   uint32_t max_viewport, max_scissor;

   /* for dynamic states that can't be emitted directly */
   uint32_t dynamic_stencil_mask;
   uint32_t dynamic_stencil_wrmask;
   uint32_t dynamic_stencil_ref;

   uint32_t gras_su_cntl, rb_depth_cntl, rb_stencil_cntl;
   uint32_t pc_raster_cntl, vpc_unknown_9107;
   enum pc_di_primtype primtype;
   bool primitive_restart_enable;

   /* saved states to re-emit in TU_CMD_DIRTY_DRAW_STATE case */
   struct tu_draw_state dynamic_state[TU_DYNAMIC_STATE_COUNT];
   struct tu_draw_state vertex_buffers;
   struct tu_draw_state shader_const[2];
   struct tu_draw_state desc_sets;

   struct tu_draw_state vs_params;

   /* Index buffer */
   uint64_t index_va;
   uint32_t max_index_count;
   uint8_t index_size;

   /* because streamout base has to be 32-byte aligned
    * there is an extra offset to deal with when it is
    * unaligned
    */
   uint8_t streamout_offset[IR3_MAX_SO_BUFFERS];

   /* Renderpasses are tricky, because we may need to flush differently if
    * using sysmem vs. gmem and therefore we have to delay any flushing that
    * happens before a renderpass. So we have to have two copies of the flush
    * state, one for intra-renderpass flushes (i.e. renderpass dependencies)
    * and one for outside a renderpass.
    */
   struct tu_cache_state cache;
   struct tu_cache_state renderpass_cache;

   enum tu_cmd_ccu_state ccu_state;

   const struct tu_render_pass *pass;
   const struct tu_subpass *subpass;
   const struct tu_framebuffer *framebuffer;
   VkRect2D render_area;

   const struct tu_image_view **attachments;

   bool xfb_used;
   bool has_tess;
   bool tessfactor_addr_set;
   bool has_subpass_predication;
   bool predication_active;
   bool disable_gmem;
   enum a5xx_line_mode line_mode;

   uint32_t drawcall_count;

   /* A calculated "draw cost" value for renderpass, which tries to
    * estimate the bandwidth-per-sample of all the draws according
    * to:
    *
    *    foreach_draw (...) {
    *      cost += num_frag_outputs;
    *      if (blend_enabled)
    *        cost += num_blend_enabled;
    *      if (depth_test_enabled)
    *        cost++;
    *      if (depth_write_enabled)
    *        cost++;
    *    }
    *
    * The idea is that each sample-passed minimally does one write
    * per MRT.  If blend is enabled, the hw will additionally do
    * a framebuffer read per sample-passed (for each MRT with blend
    * enabled).  If depth-test is enabled, the hw will additionally
    * a depth buffer read.  If depth-write is enable, the hw will
    * additionally do a depth buffer write.
    *
    * This does ignore depth buffer traffic for samples which do not
    * pass do to depth-test fail, and some other details.  But it is
    * just intended to be a rough estimate that is easy to calculate.
    */
   uint32_t total_drawcalls_cost;

   struct tu_lrz_state lrz;

   struct tu_draw_state depth_plane_state;

   struct tu_vs_params last_vs_params;
};

struct tu_cmd_pool
{
   struct vk_object_base base;

   VkAllocationCallbacks alloc;
   struct list_head cmd_buffers;
   struct list_head free_cmd_buffers;
   uint32_t queue_family_index;
};

enum tu_cmd_buffer_status
{
   TU_CMD_BUFFER_STATUS_INVALID,
   TU_CMD_BUFFER_STATUS_INITIAL,
   TU_CMD_BUFFER_STATUS_RECORDING,
   TU_CMD_BUFFER_STATUS_EXECUTABLE,
   TU_CMD_BUFFER_STATUS_PENDING,
};

struct tu_cmd_buffer
{
   struct vk_command_buffer vk;

   struct tu_device *device;

   struct tu_cmd_pool *pool;
   struct list_head pool_link;

   struct u_trace trace;
   struct u_trace_iterator trace_renderpass_start;
   struct u_trace_iterator trace_renderpass_end;

   struct list_head renderpass_autotune_results;

   VkCommandBufferUsageFlags usage_flags;
   VkCommandBufferLevel level;
   enum tu_cmd_buffer_status status;

   struct tu_cmd_state state;
   uint32_t queue_family_index;

   uint32_t push_constants[MAX_PUSH_CONSTANTS_SIZE / 4];
   VkShaderStageFlags push_constant_stages;
   struct tu_descriptor_set meta_push_descriptors;

   struct tu_descriptor_state descriptors[MAX_BIND_POINTS];

   VkResult record_result;

   struct tu_cs cs;
   struct tu_cs draw_cs;
   struct tu_cs tile_store_cs;
   struct tu_cs draw_epilogue_cs;
   struct tu_cs sub_cs;

   uint32_t vsc_draw_strm_pitch;
   uint32_t vsc_prim_strm_pitch;
};

/* Temporary struct for tracking a register state to be written, used by
 * a6xx-pack.h and tu_cs_emit_regs()
 */
struct tu_reg_value {
   uint32_t reg;
   uint64_t value;
   bool is_address;
   struct tu_bo *bo;
   bool bo_write;
   uint32_t bo_offset;
   uint32_t bo_shift;
};


void tu_emit_cache_flush_renderpass(struct tu_cmd_buffer *cmd_buffer,
                                    struct tu_cs *cs);

void tu_emit_cache_flush_ccu(struct tu_cmd_buffer *cmd_buffer,
                             struct tu_cs *cs,
                             enum tu_cmd_ccu_state ccu_state);

void
tu6_emit_event_write(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     enum vgt_event_type event);

static inline struct tu_descriptor_state *
tu_get_descriptors_state(struct tu_cmd_buffer *cmd_buffer,
                         VkPipelineBindPoint bind_point)
{
   return &cmd_buffer->descriptors[bind_point];
}

struct tu_event
{
   struct vk_object_base base;
   struct tu_bo bo;
};

struct tu_push_constant_range
{
   uint32_t lo;
   uint32_t count;
};

struct tu_shader
{
   struct ir3_shader *ir3_shader;

   struct tu_push_constant_range push_consts;
   uint8_t active_desc_sets;
   bool multi_pos_output;
};

bool
tu_nir_lower_multiview(nir_shader *nir, uint32_t mask, bool *multi_pos_output,
                       struct tu_device *dev);

nir_shader *
tu_spirv_to_nir(struct tu_device *dev,
                const VkPipelineShaderStageCreateInfo *stage_info,
                gl_shader_stage stage);

struct tu_shader *
tu_shader_create(struct tu_device *dev,
                 nir_shader *nir,
                 const VkPipelineShaderStageCreateInfo *stage_info,
                 unsigned multiview_mask,
                 struct tu_pipeline_layout *layout,
                 const VkAllocationCallbacks *alloc);

void
tu_shader_destroy(struct tu_device *dev,
                  struct tu_shader *shader,
                  const VkAllocationCallbacks *alloc);

struct tu_program_descriptor_linkage
{
   struct ir3_const_state const_state;

   uint32_t constlen;

   struct tu_push_constant_range push_consts;
};

struct tu_pipeline_executable {
   gl_shader_stage stage;

   struct ir3_info stats;
   bool is_binning;

   char *nir_from_spirv;
   char *nir_final;
   char *disasm;
};

struct tu_pipeline
{
   struct vk_object_base base;

   struct tu_cs cs;

   /* Separate BO for private memory since it should GPU writable */
   struct tu_bo pvtmem_bo;

   struct tu_pipeline_layout *layout;

   bool need_indirect_descriptor_sets;
   VkShaderStageFlags active_stages;
   uint32_t active_desc_sets;

   /* mask of enabled dynamic states
    * if BIT(i) is set, pipeline->dynamic_state[i] is *NOT* used
    */
   uint32_t dynamic_state_mask;
   struct tu_draw_state dynamic_state[TU_DYNAMIC_STATE_COUNT];

   /* for dynamic states which use the same register: */
   uint32_t gras_su_cntl, gras_su_cntl_mask;
   uint32_t rb_depth_cntl, rb_depth_cntl_mask;
   uint32_t rb_stencil_cntl, rb_stencil_cntl_mask;
   uint32_t pc_raster_cntl, pc_raster_cntl_mask;
   uint32_t vpc_unknown_9107, vpc_unknown_9107_mask;
   uint32_t stencil_wrmask;

   bool rb_depth_cntl_disable;

   enum a5xx_line_mode line_mode;

   /* draw states for the pipeline */
   struct tu_draw_state load_state, rast_state, blend_state;

   /* for vertex buffers state */
   uint32_t num_vbs;

   struct
   {
      struct tu_draw_state config_state;
      struct tu_draw_state state;
      struct tu_draw_state binning_state;

      struct tu_program_descriptor_linkage link[MESA_SHADER_STAGES];
   } program;

   struct
   {
      struct tu_draw_state state;
      struct tu_draw_state binning_state;
   } vi;

   struct
   {
      enum pc_di_primtype primtype;
      bool primitive_restart;
   } ia;

   struct
   {
      uint32_t patch_type;
      uint32_t param_stride;
      bool upper_left_domain_origin;
   } tess;

   struct
   {
      uint32_t local_size[3];
      uint32_t subgroup_size;
   } compute;

   bool provoking_vertex_last;

   struct tu_lrz_pipeline lrz;

   /* Base drawcall cost for sysmem vs gmem autotuner */
   uint8_t drawcall_base_cost;

   void *executables_mem_ctx;
   /* tu_pipeline_executable */
   struct util_dynarray executables;
};

void
tu6_emit_viewport(struct tu_cs *cs, const VkViewport *viewport, uint32_t num_viewport);

void
tu6_emit_scissor(struct tu_cs *cs, const VkRect2D *scs, uint32_t scissor_count);

void
tu6_clear_lrz(struct tu_cmd_buffer *cmd, struct tu_cs *cs, struct tu_image* image, const VkClearValue *value);

void
tu6_emit_sample_locations(struct tu_cs *cs, const VkSampleLocationsInfoEXT *samp_loc);

void
tu6_emit_depth_bias(struct tu_cs *cs,
                    float constant_factor,
                    float clamp,
                    float slope_factor);

void tu6_emit_msaa(struct tu_cs *cs, VkSampleCountFlagBits samples,
                   enum a5xx_line_mode line_mode);

void tu6_emit_window_scissor(struct tu_cs *cs, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);

void tu6_emit_window_offset(struct tu_cs *cs, uint32_t x1, uint32_t y1);

void tu_disable_draw_states(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

void tu6_apply_depth_bounds_workaround(struct tu_device *device,
                                       uint32_t *rb_depth_cntl);

struct tu_pvtmem_config {
   uint64_t iova;
   uint32_t per_fiber_size;
   uint32_t per_sp_size;
   bool per_wave;
};

void
tu6_emit_xs_config(struct tu_cs *cs,
                   gl_shader_stage stage,
                   const struct ir3_shader_variant *xs);

void
tu6_emit_xs(struct tu_cs *cs,
            gl_shader_stage stage,
            const struct ir3_shader_variant *xs,
            const struct tu_pvtmem_config *pvtmem,
            uint64_t binary_iova);

void
tu6_emit_vpc(struct tu_cs *cs,
             const struct ir3_shader_variant *vs,
             const struct ir3_shader_variant *hs,
             const struct ir3_shader_variant *ds,
             const struct ir3_shader_variant *gs,
             const struct ir3_shader_variant *fs,
             uint32_t patch_control_points);

void
tu6_emit_fs_inputs(struct tu_cs *cs, const struct ir3_shader_variant *fs);

struct tu_image_view;

void
tu_resolve_sysmem(struct tu_cmd_buffer *cmd,
                  struct tu_cs *cs,
                  const struct tu_image_view *src,
                  const struct tu_image_view *dst,
                  uint32_t layer_mask,
                  uint32_t layers,
                  const VkRect2D *rect);

void
tu_clear_sysmem_attachment(struct tu_cmd_buffer *cmd,
                           struct tu_cs *cs,
                           uint32_t a,
                           const VkRenderPassBeginInfo *info);

void
tu_clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         const VkRenderPassBeginInfo *info);

void
tu_load_gmem_attachment(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t a,
                        bool force_load);

/* expose this function to be able to emit load without checking LOAD_OP */
void
tu_emit_load_gmem_attachment(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint32_t a);

/* note: gmem store can also resolve */
void
tu_store_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         uint32_t gmem_a);

enum pipe_format tu_vk_format_to_pipe_format(VkFormat vk_format);

struct tu_native_format
{
   enum a6xx_format fmt : 8;
   enum a3xx_color_swap swap : 8;
   enum a6xx_tile_mode tile_mode : 8;
};

enum pipe_format tu_vk_format_to_pipe_format(VkFormat vk_format);
bool tu6_format_vtx_supported(VkFormat format);
struct tu_native_format tu6_format_vtx(VkFormat format);
bool tu6_format_color_supported(enum pipe_format format);
struct tu_native_format tu6_format_color(enum pipe_format format, enum a6xx_tile_mode tile_mode);
bool tu6_format_texture_supported(enum pipe_format format);
struct tu_native_format tu6_format_texture(enum pipe_format format, enum a6xx_tile_mode tile_mode);

static inline enum a6xx_format
tu6_base_format(enum pipe_format format)
{
   /* note: tu6_format_color doesn't care about tiling for .fmt field */
   return tu6_format_color(format, TILE6_LINEAR).fmt;
}

struct tu_image
{
   struct vk_object_base base;

   /* The original VkFormat provided by the client.  This may not match any
    * of the actual surface formats.
    */
   VkFormat vk_format;
   uint32_t level_count;
   uint32_t layer_count;

   struct fdl_layout layout[3];
   uint32_t total_size;

#ifdef ANDROID
   /* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
   VkDeviceMemory owned_memory;
#endif

   /* Set when bound */
   struct tu_bo *bo;
   uint64_t iova;

   uint32_t lrz_height;
   uint32_t lrz_pitch;
   uint32_t lrz_offset;

   bool shareable;
};

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

enum pipe_format tu6_plane_format(VkFormat format, uint32_t plane);

uint32_t tu6_plane_index(VkFormat format, VkImageAspectFlags aspect_mask);

enum pipe_format tu_format_for_aspect(enum pipe_format format,
                                      VkImageAspectFlags aspect_mask);

struct tu_image_view
{
   struct vk_object_base base;

   struct tu_image *image; /**< VkImageViewCreateInfo::image */

   struct fdl6_view view;

   /* for d32s8 separate stencil */
   uint64_t stencil_base_addr;
   uint32_t stencil_layer_size;
   uint32_t stencil_PITCH;
};

struct tu_sampler_ycbcr_conversion {
   struct vk_object_base base;

   VkFormat format;
   VkSamplerYcbcrModelConversion ycbcr_model;
   VkSamplerYcbcrRange ycbcr_range;
   VkComponentMapping components;
   VkChromaLocation chroma_offsets[2];
   VkFilter chroma_filter;
};

struct tu_sampler {
   struct vk_object_base base;

   uint32_t descriptor[A6XX_TEX_SAMP_DWORDS];
   struct tu_sampler_ycbcr_conversion *ycbcr_sampler;
};

void
tu_cs_image_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer);

void
tu_cs_image_ref_2d(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer, bool src);

void
tu_cs_image_flag_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer);

void
tu_cs_image_stencil_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer);

#define tu_image_view_stencil(iview, x) \
   ((iview->view.x & ~A6XX_##x##_COLOR_FORMAT__MASK) | A6XX_##x##_COLOR_FORMAT(FMT6_8_UINT))

VkResult
tu_gralloc_info(struct tu_device *device,
                const VkNativeBufferANDROID *gralloc_info,
                int *dma_buf,
                uint64_t *modifier);

VkResult
tu_import_memory_from_gralloc_handle(VkDevice device_h,
                                     int dma_buf,
                                     const VkAllocationCallbacks *alloc,
                                     VkImage image_h);

void
tu_image_view_init(struct tu_image_view *iview,
                   const VkImageViewCreateInfo *pCreateInfo,
                   bool limited_z24s8);

bool
tiling_possible(VkFormat format);

bool
ubwc_possible(VkFormat format, VkImageType type, VkImageUsageFlags usage, VkImageUsageFlags stencil_usage,
              const struct fd_dev_info *info, VkSampleCountFlagBits samples);

struct tu_buffer_view
{
   struct vk_object_base base;

   uint32_t descriptor[A6XX_TEX_CONST_DWORDS];

   struct tu_buffer *buffer;
};
void
tu_buffer_view_init(struct tu_buffer_view *view,
                    struct tu_device *device,
                    const VkBufferViewCreateInfo *pCreateInfo);

struct tu_attachment_info
{
   struct tu_image_view *attachment;
};

struct tu_framebuffer
{
   struct vk_object_base base;

   uint32_t width;
   uint32_t height;
   uint32_t layers;

   /* size of the first tile */
   VkExtent2D tile0;
   /* number of tiles */
   VkExtent2D tile_count;

   /* size of the first VSC pipe */
   VkExtent2D pipe0;
   /* number of VSC pipes */
   VkExtent2D pipe_count;

   /* pipe register values */
   uint32_t pipe_config[MAX_VSC_PIPES];
   uint32_t pipe_sizes[MAX_VSC_PIPES];

   uint32_t attachment_count;
   struct tu_attachment_info attachments[0];
};

void
tu_framebuffer_tiling_config(struct tu_framebuffer *fb,
                             const struct tu_device *device,
                             const struct tu_render_pass *pass);

struct tu_subpass_barrier {
   VkPipelineStageFlags src_stage_mask;
   VkPipelineStageFlags dst_stage_mask;
   VkAccessFlags src_access_mask;
   VkAccessFlags dst_access_mask;
   bool incoherent_ccu_color, incoherent_ccu_depth;
};

struct tu_subpass_attachment
{
   uint32_t attachment;

   /* For input attachments, true if it needs to be patched to refer to GMEM
    * in GMEM mode. This is false if it hasn't already been written as an
    * attachment.
    */
   bool patch_input_gmem;
};

struct tu_subpass
{
   uint32_t input_count;
   uint32_t color_count;
   uint32_t resolve_count;
   bool resolve_depth_stencil;

   /* True if there is any feedback loop at all. */
   bool feedback;

   /* True if we must invalidate UCHE thanks to a feedback loop. */
   bool feedback_invalidate;

   struct tu_subpass_attachment *input_attachments;
   struct tu_subpass_attachment *color_attachments;
   struct tu_subpass_attachment *resolve_attachments;
   struct tu_subpass_attachment depth_stencil_attachment;

   VkSampleCountFlagBits samples;

   uint32_t srgb_cntl;
   uint32_t multiview_mask;

   struct tu_subpass_barrier start_barrier;
};

struct tu_render_pass_attachment
{
   VkFormat format;
   uint32_t samples;
   uint32_t cpp;
   VkImageAspectFlags clear_mask;
   uint32_t clear_views;
   bool load;
   bool store;
   int32_t gmem_offset;
   /* for D32S8 separate stencil: */
   bool load_stencil;
   bool store_stencil;
   int32_t gmem_offset_stencil;
};

struct tu_render_pass
{
   struct vk_object_base base;

   uint32_t attachment_count;
   uint32_t subpass_count;
   uint32_t gmem_pixels;
   uint32_t tile_align_w;
   struct tu_subpass_attachment *subpass_attachments;
   struct tu_render_pass_attachment *attachments;
   struct tu_subpass_barrier end_barrier;
   struct tu_subpass subpasses[0];
};

#define PERF_CNTRS_REG 4

struct tu_perf_query_data
{
   uint32_t gid;      /* group-id */
   uint32_t cid;      /* countable-id within the group */
   uint32_t cntr_reg; /* counter register within the group */
   uint32_t pass;     /* pass index that countables can be requested */
   uint32_t app_idx;  /* index provided by apps */
};

struct tu_query_pool
{
   struct vk_object_base base;

   VkQueryType type;
   uint32_t stride;
   uint64_t size;
   uint32_t pipeline_statistics;
   struct tu_bo bo;

   /* For performance query */
   const struct fd_perfcntr_group *perf_group;
   uint32_t perf_group_count;
   uint32_t counter_index_count;
   struct tu_perf_query_data perf_query_data[0];
};

uint32_t
tu_subpass_get_attachment_to_resolve(const struct tu_subpass *subpass, uint32_t index);

void
tu_update_descriptor_sets(const struct tu_device *device,
                          VkDescriptorSet overrideSet,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies);

void
tu_update_descriptor_set_with_template(
   const struct tu_device *device,
   struct tu_descriptor_set *set,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData);

VkResult
tu_physical_device_init(struct tu_physical_device *device,
                        struct tu_instance *instance);
VkResult
tu_enumerate_devices(struct tu_instance *instance);

int
tu_device_get_gpu_timestamp(struct tu_device *dev,
                            uint64_t *ts);

int
tu_device_get_suspend_count(struct tu_device *dev,
                            uint64_t *suspend_count);

int
tu_drm_submitqueue_new(const struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id);

void
tu_drm_submitqueue_close(const struct tu_device *dev, uint32_t queue_id);

int
tu_signal_syncs(struct tu_device *device, struct vk_sync *sync1, struct vk_sync *sync2);

int
tu_syncobj_to_fd(struct tu_device *device, struct vk_sync *sync);

VkResult
tu_queue_submit(struct vk_queue *vk_queue, struct vk_queue_submit *submit);

void
tu_copy_timestamp_buffer(struct u_trace_context *utctx, void *cmdstream,
                         void *ts_from, uint32_t from_offset,
                         void *ts_to, uint32_t to_offset,
                         uint32_t count);


VkResult
tu_create_copy_timestamp_cs(struct tu_cmd_buffer *cmdbuf, struct tu_cs** cs,
                            struct u_trace **trace_copy);

/* If we copy trace and timestamps we will have to free them. */
struct tu_u_trace_cmd_data
{
   struct tu_cs *timestamp_copy_cs;
   struct u_trace *trace;
};

/* Data necessary to retrieve timestamps and clean all
 * associated resources afterwards.
 */
struct tu_u_trace_submission_data
{
   uint32_t submission_id;
   /* We have to know when timestamps are available,
    * this sync object indicates it.
    */
   struct tu_u_trace_syncobj *syncobj;

   uint32_t cmd_buffer_count;
   uint32_t last_buffer_with_tracepoints;
   struct tu_u_trace_cmd_data *cmd_trace_data;
};

VkResult
tu_u_trace_submission_data_create(
   struct tu_device *device,
   struct tu_cmd_buffer **cmd_buffers,
   uint32_t cmd_buffer_count,
   struct tu_u_trace_submission_data **submission_data);

void
tu_u_trace_submission_data_finish(
   struct tu_device *device,
   struct tu_u_trace_submission_data *submission_data);

#define TU_FROM_HANDLE(__tu_type, __name, __handle)                          \
   VK_FROM_HANDLE(__tu_type, __name, __handle)

VK_DEFINE_HANDLE_CASTS(tu_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)
VK_DEFINE_HANDLE_CASTS(tu_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(tu_instance, vk.base, VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(tu_physical_device, vk.base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(tu_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VK_DEFINE_NONDISP_HANDLE_CASTS(tu_cmd_pool, base, VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer, base, VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer_view, base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set_layout, base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_update_template, base,
                               VkDescriptorUpdateTemplate,
                               VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_device_memory, base, VkDeviceMemory,
                               VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_framebuffer, base, VkFramebuffer,
                               VK_OBJECT_TYPE_FRAMEBUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_image, base, VkImage, VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_image_view, base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW);
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_cache, base, VkPipelineCache,
                               VK_OBJECT_TYPE_PIPELINE_CACHE)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_layout, base, VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_query_pool, base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_render_pass, base, VkRenderPass,
                               VK_OBJECT_TYPE_RENDER_PASS)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_sampler, base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_sampler_ycbcr_conversion, base, VkSamplerYcbcrConversion,
                               VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION)

/* for TU_FROM_HANDLE with both VkFence and VkSemaphore: */
#define tu_syncobj_from_handle(x) ((struct tu_syncobj*) (uintptr_t) (x))

void
update_stencil_mask(uint32_t *value, VkStencilFaceFlags face, uint32_t mask);

#endif /* TU_PRIVATE_H */
