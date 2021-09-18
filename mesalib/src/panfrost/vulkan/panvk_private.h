/*
 * Copyright © 2021 Collabora Ltd.
 *
 * derived from tu_private.h driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#ifndef PANVK_PRIVATE_H
#define PANVK_PRIVATE_H

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
#include "util/list.h"
#include "util/macros.h"
#include "vk_alloc.h"
#include "vk_device.h"
#include "vk_instance.h"
#include "vk_object.h"
#include "vk_physical_device.h"
#include "wsi_common.h"

#include "drm-uapi/panfrost_drm.h"

#include "pan_blend.h"
#include "pan_cs.h"
#include "pan_device.h"
#include "panvk_mempool.h"
#include "pan_texture.h"
#include "pan_scoreboard.h"
#include "vk_extensions.h"
#include "panvk_varyings.h"

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>

#include "panvk_entrypoints.h"

#define MAX_BIND_POINTS 2 /* compute + graphics */
#define MAX_VBS 16
#define MAX_VERTEX_ATTRIBS 16
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
#define PANVK_MAX_DRM_DEVICES 1
#define MAX_VIEWS 8

#define NUM_DEPTH_CLEAR_PIPELINES 3

#define panvk_printflike(a, b) __attribute__((__format__(__printf__, a, b)))

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

struct panvk_instance;

VkResult
__vk_errorf(struct panvk_instance *instance,
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
panvk_logi(const char *format, ...) panvk_printflike(1, 2);
void
panvk_logi_v(const char *format, va_list va);

#define panvk_stub() assert(!"stub")

struct panvk_meta {
   struct panvk_pool bin_pool;
   struct panvk_pool desc_pool;

   /* Access to the blitter pools are protected by the blitter
    * shader/rsd locks. They can't be merged with other binary/desc
    * pools unless we patch pan_blitter.c to external pool locks.
    */
   struct {
      struct panvk_pool bin_pool;
      struct panvk_pool desc_pool;
   } blitter;

   struct {
      mali_ptr shader;
      struct pan_shader_info shader_info;
   } clear_attachment[MAX_RTS][3]; /* 3 base types */
};

struct panvk_physical_device {
   struct vk_physical_device vk;

   /* The API agnostic device object. */
   struct panfrost_device pdev;

   struct panvk_instance *instance;

   char path[20];
   char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t cache_uuid[VK_UUID_SIZE];

   struct wsi_device wsi_device;
   struct panvk_meta meta;

   int master_fd;
};

enum panvk_debug_flags {
   PANVK_DEBUG_STARTUP = 1 << 0,
   PANVK_DEBUG_NIR = 1 << 1,
   PANVK_DEBUG_TRACE = 1 << 2,
   PANVK_DEBUG_SYNC = 1 << 3,
   PANVK_DEBUG_AFBC = 1 << 4,
   PANVK_DEBUG_LINEAR = 1 << 5,
};

struct panvk_instance {
   struct vk_instance vk;

   uint32_t api_version;
   int physical_device_count;
   struct panvk_physical_device physical_devices[PANVK_MAX_DRM_DEVICES];

   enum panvk_debug_flags debug_flags;
};

VkResult
panvk_wsi_init(struct panvk_physical_device *physical_device);
void
panvk_wsi_finish(struct panvk_physical_device *physical_device);

bool
panvk_instance_extension_supported(const char *name);
uint32_t
panvk_physical_device_api_version(struct panvk_physical_device *dev);
bool
panvk_physical_device_extension_supported(struct panvk_physical_device *dev,
                                       const char *name);

struct panvk_pipeline_cache {
   struct vk_object_base base;
   VkAllocationCallbacks alloc;
};

/* queue types */
#define PANVK_QUEUE_GENERAL 0

#define PANVK_MAX_QUEUE_FAMILIES 1

struct panvk_queue {
   struct vk_object_base base;
   struct panvk_device *device;
   uint32_t queue_family_index;
   VkDeviceQueueCreateFlags flags;
   uint32_t sync;
};

struct panvk_device {
   struct vk_device vk;

   struct panvk_instance *instance;

   struct panvk_queue *queues[PANVK_MAX_QUEUE_FAMILIES];
   int queue_count[PANVK_MAX_QUEUE_FAMILIES];

   struct panvk_physical_device *physical_device;
   int _lost;
};

VkResult _panvk_device_set_lost(struct panvk_device *device,
                                const char *file, int line,
                                const char *msg, ...) PRINTFLIKE(4, 5);
#define panvk_device_set_lost(dev, ...) \
   _panvk_device_set_lost(dev, __FILE__, __LINE__, __VA_ARGS__)

static inline bool
panvk_device_is_lost(struct panvk_device *device)
{
   return unlikely(p_atomic_read(&device->_lost));
}

#define TILER_DESC_WORDS 56 

struct panvk_batch {
   struct list_head node;
   struct util_dynarray jobs;
   struct util_dynarray event_ops;
   struct pan_scoreboard scoreboard;
   struct {
      const struct panvk_framebuffer *info;
      struct panfrost_ptr desc;
   } fb;
   struct {
      struct panfrost_bo *src, *dst;
   } blit;
   struct panfrost_ptr tls;
   mali_ptr fragment_job;
   struct {
      struct pan_tiler_context ctx;
      struct panfrost_ptr descs;
      uint32_t templ[TILER_DESC_WORDS];
   } tiler;
   bool issued;
};

struct panvk_syncobj {
   uint32_t permanent, temporary;
};

enum panvk_event_op_type {
   PANVK_EVENT_OP_SET,
   PANVK_EVENT_OP_RESET,
   PANVK_EVENT_OP_WAIT,
};

struct panvk_event_op {
   enum panvk_event_op_type type;
   struct panvk_event *event;
};

struct panvk_fence {
   struct vk_object_base base;
   struct panvk_syncobj syncobj;
};

struct panvk_semaphore {
   struct vk_object_base base;
   struct panvk_syncobj syncobj;
};

int
panvk_signal_syncobjs(struct panvk_device *device,
                      struct panvk_syncobj *syncobj1,
                      struct panvk_syncobj *syncobj2);

int
panvk_syncobj_to_fd(struct panvk_device *device,
                    struct panvk_syncobj *sync);

struct panvk_device_memory {
   struct vk_object_base base;
   struct panfrost_bo *bo;
};

struct panvk_descriptor {
   union {
      struct {
         VkImageLayout layout;
         struct panvk_image_view *view;
         struct panvk_sampler *sampler;
      } image;

      struct {
         struct panvk_buffer *buffer;
         uint64_t offset;
         uint64_t range;
      } buffer_info;

      struct panvk_buffer_view *buffer_view;
   };
};

struct panvk_descriptor_set {
   struct vk_object_base base;
   struct panvk_descriptor_pool *pool;
   const struct panvk_descriptor_set_layout *layout;
   struct panvk_descriptor *descs;
   void *ubos;
   void *samplers;
   void *textures;
};

#define MAX_SETS 4

struct panvk_descriptor_set_binding_layout {
   VkDescriptorType type;

   /* Number of array elements in this binding */
   unsigned array_size;

   /* Indices in the desc arrays */
   unsigned desc_idx;
   union {
      struct {
         unsigned sampler_idx;
         unsigned tex_idx;
      };
      struct {
         union {
            unsigned ssbo_idx;
            unsigned ubo_idx;
         };
         unsigned dynoffset_idx;
      };
   };

   /* Shader stages affected by this set+binding */
   uint16_t shader_stages;

   struct panvk_sampler **immutable_samplers;
};

struct panvk_descriptor_set_layout {
   struct vk_object_base base;

   /* The create flags for this descriptor set layout */
   VkDescriptorSetLayoutCreateFlags flags;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;

   unsigned num_descs;
   unsigned num_samplers;
   unsigned num_textures;
   unsigned num_ubos;
   unsigned num_ssbos;
   unsigned num_dynoffsets;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Bindings in this descriptor set */
   struct panvk_descriptor_set_binding_layout bindings[0];
};

struct panvk_pipeline_layout {
   struct vk_object_base base;
   unsigned char sha1[20];

   unsigned num_samplers;
   unsigned num_textures;
   unsigned num_ubos;
   unsigned num_ssbos;
   unsigned num_dynoffsets;
   uint32_t num_sets;

   struct {
      struct panvk_descriptor_set_layout *layout;
      unsigned sampler_offset;
      unsigned tex_offset;
      unsigned ubo_offset;
      unsigned ssbo_offset;
      unsigned dynoffset_offset;
   } sets[MAX_SETS];
};

struct panvk_desc_pool_counters {
   unsigned samplers;
   unsigned combined_image_samplers;
   unsigned sampled_images;
   unsigned storage_images;
   unsigned uniform_texel_bufs;
   unsigned storage_texel_bufs;
   unsigned input_attachments;
   unsigned uniform_bufs;
   unsigned storage_bufs;
   unsigned uniform_dyn_bufs;
   unsigned storage_dyn_bufs;
   unsigned sets;
};

struct panvk_descriptor_pool {
   struct vk_object_base base;
   struct panvk_desc_pool_counters max;
   struct panvk_desc_pool_counters cur;
   struct panvk_descriptor_set *sets;
};

struct panvk_buffer {
   struct vk_object_base base;
   VkDeviceSize size;

   VkBufferUsageFlags usage;
   VkBufferCreateFlags flags;

   struct panfrost_bo *bo;
   VkDeviceSize bo_offset;
};

enum panvk_dynamic_state_bits {
   PANVK_DYNAMIC_VIEWPORT = 1 << 0,
   PANVK_DYNAMIC_SCISSOR = 1 << 1,
   PANVK_DYNAMIC_LINE_WIDTH = 1 << 2,
   PANVK_DYNAMIC_DEPTH_BIAS = 1 << 3,
   PANVK_DYNAMIC_BLEND_CONSTANTS = 1 << 4,
   PANVK_DYNAMIC_DEPTH_BOUNDS = 1 << 5,
   PANVK_DYNAMIC_STENCIL_COMPARE_MASK = 1 << 6,
   PANVK_DYNAMIC_STENCIL_WRITE_MASK = 1 << 7,
   PANVK_DYNAMIC_STENCIL_REFERENCE = 1 << 8,
   PANVK_DYNAMIC_DISCARD_RECTANGLE = 1 << 9,
   PANVK_DYNAMIC_ALL = (1 << 10) - 1,
};

struct panvk_descriptor_state {
   struct {
      const struct panvk_descriptor_set *set;
      struct panfrost_ptr dynoffsets;
   } sets[MAX_SETS];
   mali_ptr sysvals[MESA_SHADER_STAGES];
   mali_ptr ubos;
   mali_ptr textures;
   mali_ptr samplers;
};

#define INVOCATION_DESC_WORDS 2

struct panvk_draw_info {
   unsigned first_index;
   unsigned index_count;
   unsigned first_vertex;
   unsigned vertex_count;
   unsigned padded_vertex_count;
   unsigned first_instance;
   unsigned instance_count;
   int vertex_offset;
   unsigned offset_start;
   uint32_t invocation[INVOCATION_DESC_WORDS];
   struct {
      mali_ptr varyings;
      mali_ptr attributes;
      mali_ptr push_constants;
   } stages[MESA_SHADER_STAGES];
   mali_ptr varying_bufs;
   mali_ptr attribute_bufs;
   mali_ptr textures;
   mali_ptr samplers;
   mali_ptr ubos;
   mali_ptr position;
   union {
      mali_ptr psiz;
      float line_width;
   };
   mali_ptr tls;
   mali_ptr fb;
   const struct pan_tiler_context *tiler_ctx;
   mali_ptr fs_rsd;
   mali_ptr viewport;
   struct {
      struct panfrost_ptr vertex;
      struct panfrost_ptr tiler;
   } jobs;
};

struct panvk_attrib_info {
   unsigned buf;
   unsigned offset;
   enum pipe_format format;
};

struct panvk_attrib_buf_info {
   bool special;
   union {
      struct {
         unsigned stride;
         bool per_instance;
      };
      unsigned special_id;
   };
};

struct panvk_attribs_info {
   struct panvk_attrib_info attrib[PAN_MAX_ATTRIBUTE];
   unsigned attrib_count;
   struct panvk_attrib_buf_info buf[PAN_MAX_ATTRIBUTE];
   unsigned buf_count;
};

struct panvk_attrib_buf {
   mali_ptr address;
   unsigned size;
};

struct panvk_cmd_state {
   VkPipelineBindPoint bind_point;

   struct panvk_pipeline *pipeline;

   uint32_t dirty;

   struct panvk_varyings_info varyings;
   mali_ptr fs_rsd;

   struct {
      float constants[4];
   } blend;

   struct {
      struct pan_compute_dim wg_count;
   } compute;

   struct {
      struct {
         float constant_factor;
         float clamp;
         float slope_factor;
      } depth_bias;
      float line_width;
   } rast;

   struct {
      struct panvk_attrib_buf bufs[MAX_VBS];
      unsigned count;
      mali_ptr attribs;
      mali_ptr attrib_bufs;
   } vb;

   /* Index buffer */
   struct {
      struct panvk_buffer *buffer;
      uint64_t offset;
      uint32_t type;
      uint32_t max_index_count;
      uint8_t index_size;
      uint64_t index_va;
   } ib;

   struct {
      struct {
         uint8_t compare_mask;
         uint8_t write_mask;
         uint8_t ref;
      } s_front, s_back;
   } zs;

   const struct panvk_render_pass *pass;
   const struct panvk_subpass *subpass;
   const struct panvk_framebuffer *framebuffer;
   VkRect2D render_area;

   struct panvk_clear_value *clear;

   mali_ptr vpd;
   VkViewport viewport;
   VkRect2D scissor;

   struct panvk_batch *batch;
};

struct panvk_cmd_pool {
   struct vk_object_base base;
   VkAllocationCallbacks alloc;
   struct list_head active_cmd_buffers;
   struct list_head free_cmd_buffers;
   uint32_t queue_family_index;
   struct panvk_bo_pool desc_bo_pool;
   struct panvk_bo_pool varying_bo_pool;
   struct panvk_bo_pool tls_bo_pool;
};

enum panvk_cmd_buffer_status {
   PANVK_CMD_BUFFER_STATUS_INVALID,
   PANVK_CMD_BUFFER_STATUS_INITIAL,
   PANVK_CMD_BUFFER_STATUS_RECORDING,
   PANVK_CMD_BUFFER_STATUS_EXECUTABLE,
   PANVK_CMD_BUFFER_STATUS_PENDING,
};

struct panvk_cmd_buffer {
   struct vk_object_base base;

   struct panvk_device *device;

   struct panvk_cmd_pool *pool;
   struct list_head pool_link;
   struct panvk_pool desc_pool;
   struct panvk_pool varying_pool;
   struct panvk_pool tls_pool;
   struct list_head batches;

   VkCommandBufferUsageFlags usage_flags;
   VkCommandBufferLevel level;
   enum panvk_cmd_buffer_status status;

   struct panvk_cmd_state state;
   uint32_t queue_family_index;

   uint8_t push_constants[MAX_PUSH_CONSTANTS_SIZE];
   VkShaderStageFlags push_constant_stages;
   struct panvk_descriptor_set meta_push_descriptors;

   struct panvk_descriptor_state descriptors[MAX_BIND_POINTS];

   VkResult record_result;
};

void
panvk_cmd_open_batch(struct panvk_cmd_buffer *cmdbuf);

void
panvk_pack_color(struct panvk_clear_value *out,
                 const VkClearColorValue *in,
                 enum pipe_format format);

struct panvk_event {
   struct vk_object_base base;
   uint32_t syncobj;
};

struct panvk_shader_module {
   struct vk_object_base base;
   unsigned char sha1[20];

   uint32_t code_size;
   const uint32_t *code[0];
};

struct panvk_shader {
   struct pan_shader_info info;
   struct util_dynarray binary;
   unsigned sysval_ubo;
};

struct panvk_shader *
panvk_shader_create(struct panvk_device *dev,
                    gl_shader_stage stage,
                    const VkPipelineShaderStageCreateInfo *stage_info,
                    const struct panvk_pipeline_layout *layout,
                    unsigned sysval_ubo,
                    struct pan_blend_state *blend_state,
                    bool static_blend_constants,
                    const VkAllocationCallbacks *alloc);

void
panvk_shader_destroy(struct panvk_device *dev,
                     struct panvk_shader *shader,
                     const VkAllocationCallbacks *alloc);

union panvk_sysval_data {
   float f32[4];
   double f64[2];
   uint32_t u32[4];
   uint64_t u64[2];
};

#define RSD_WORDS 16
#define BLEND_DESC_WORDS 4

struct panvk_pipeline {
   struct vk_object_base base;

   struct panvk_varyings_info varyings;
   struct panvk_attribs_info attribs;

   const struct panvk_pipeline_layout *layout;

   unsigned active_stages;

   uint32_t dynamic_state_mask;

   struct panfrost_bo *binary_bo;
   struct panfrost_bo *state_bo;

   mali_ptr vpd;
   mali_ptr rsds[MESA_SHADER_STAGES];

   unsigned num_ubos;
   unsigned num_sysvals;

   struct {
      unsigned ubo_idx;
      mali_ptr ubo;
      struct panfrost_sysvals ids;
      uint32_t dirty_mask;
   } sysvals[MESA_SHADER_STAGES];

   unsigned tls_size;
   unsigned wls_size;

   struct {
      mali_ptr address;
      struct pan_shader_info info;
      uint32_t rsd_template[RSD_WORDS];
      bool required;
      bool dynamic_rsd;
      uint8_t rt_mask;
   } fs;

   struct {
      unsigned topology;
      bool writes_point_size;
      bool primitive_restart;
   } ia;

   struct {
      bool clamp_depth;
      float line_width;
      struct {
         bool enable;
         float constant_factor;
         float clamp;
         float slope_factor;
      } depth_bias;
      bool front_ccw;
      bool cull_front_face;
      bool cull_back_face;
   } rast;

   struct {
      bool z_test;
      bool z_write;
      unsigned z_compare_func;
      bool s_test;
      struct {
         unsigned fail_op;
         unsigned pass_op;
         unsigned z_fail_op;
         unsigned compare_func;
         uint8_t compare_mask;
         uint8_t write_mask;
         uint8_t ref;
      } s_front, s_back;
   } zs;

   struct {
      uint8_t rast_samples;
      uint8_t min_samples;
      uint16_t sample_mask;
      bool alpha_to_coverage;
      bool alpha_to_one;
   } ms;

   struct {
      struct pan_blend_state state;
      uint32_t bd_template[8][BLEND_DESC_WORDS];
      struct {
         uint8_t index;
         uint16_t bifrost_factor;
      } constant[8];
      bool reads_dest;
   } blend;

   VkViewport viewport;
   VkRect2D scissor;
};

struct panvk_image_level {
   VkDeviceSize offset;
   VkDeviceSize size;
   uint32_t pitch;
};

struct panvk_slice_layout {
   unsigned width;
   unsigned height;
   unsigned depth;
   unsigned offset;
   unsigned line_stride;
   unsigned size;

   /* If there is a header preceding each slice, how big is
    * that header? Used for AFBC.
    */
   unsigned afbc_header_size;

   /* If checksumming is enabled following the slice, what
    * is its offset/stride?
    */
   struct {
      unsigned offset;
      unsigned stride;
      unsigned size;
   } checksum;
};

#define PANVK_MAX_MIP_LEVELS 13

struct panvk_plane_layout {
   struct panvk_slice_layout slices[PANVK_MAX_MIP_LEVELS];
   unsigned offset;
   unsigned array_stride;
   unsigned size;
};

struct panvk_plane_memory {
   const struct panfrost_bo *bo;
   unsigned offset;
};

#define PANVK_MAX_PLANES 1

struct panvk_image {
   struct vk_object_base base;
   struct pan_image pimage;
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

   unsigned queue_family_mask;
   bool exclusive;
   bool shareable;
};

unsigned
panvk_image_get_plane_size(const struct panvk_image *image, unsigned plane);

unsigned
panvk_image_get_total_size(const struct panvk_image *image);

#define TEXTURE_DESC_WORDS 8

struct panvk_image_view {
   struct vk_object_base base;
   struct pan_image_view pview;

   VkFormat vk_format;
   struct panfrost_bo *bo;
   uint32_t desc[TEXTURE_DESC_WORDS];
};

#define SAMPLER_DESC_WORDS 8

struct panvk_sampler {
   struct vk_object_base base;
   uint32_t desc[SAMPLER_DESC_WORDS];
};

struct panvk_buffer_view {
   struct vk_object_base base;
};

struct panvk_attachment_info {
   struct panvk_image_view *iview;
};

struct panvk_framebuffer {
   struct vk_object_base base;

   uint32_t width;
   uint32_t height;
   uint32_t layers;

   uint32_t attachment_count;
   struct panvk_attachment_info attachments[0];
};

struct panvk_clear_value {
   union {
      uint32_t color[4];
      struct {
         float depth;
         uint8_t stencil;
      };
   };
};

struct panvk_subpass_attachment {
   uint32_t idx;
   VkImageLayout layout;
   bool clear;
};

struct panvk_subpass {
   uint32_t input_count;
   uint32_t color_count;
   struct panvk_subpass_attachment *input_attachments;
   uint8_t active_color_attachments;
   struct panvk_subpass_attachment *color_attachments;
   struct panvk_subpass_attachment *resolve_attachments;
   struct panvk_subpass_attachment zs_attachment;

   uint32_t view_mask;
};

struct panvk_render_pass_attachment {
   VkAttachmentDescriptionFlags flags;
   enum pipe_format format;
   unsigned samples;
   VkAttachmentLoadOp load_op;
   VkAttachmentStoreOp store_op;
   VkAttachmentLoadOp stencil_load_op;
   VkAttachmentStoreOp stencil_store_op;
   VkImageLayout initial_layout;
   VkImageLayout final_layout;
   unsigned view_mask;
   unsigned clear_subpass;
};

struct panvk_render_pass {
   struct vk_object_base base;

   uint32_t attachment_count;
   uint32_t subpass_count;
   struct panvk_subpass_attachment *subpass_attachments;
   struct panvk_render_pass_attachment *attachments;
   struct panvk_subpass subpasses[0];
};

VK_DEFINE_HANDLE_CASTS(panvk_cmd_buffer, base, VkCommandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER)
VK_DEFINE_HANDLE_CASTS(panvk_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(panvk_instance, vk.base, VkInstance, VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(panvk_physical_device, vk.base, VkPhysicalDevice, VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(panvk_queue, base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_cmd_pool, base, VkCommandPool, VK_OBJECT_TYPE_COMMAND_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_buffer, base, VkBuffer, VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_buffer_view, base, VkBufferView, VK_OBJECT_TYPE_BUFFER_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_pool, base, VkDescriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_set, base, VkDescriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_set_layout, base,
                               VkDescriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_device_memory, base, VkDeviceMemory, VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_fence, base, VkFence, VK_OBJECT_TYPE_FENCE)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_framebuffer, base, VkFramebuffer, VK_OBJECT_TYPE_FRAMEBUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_image, base, VkImage, VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_image_view, base, VkImageView, VK_OBJECT_TYPE_IMAGE_VIEW);
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_pipeline_cache, base, VkPipelineCache, VK_OBJECT_TYPE_PIPELINE_CACHE)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_pipeline, base, VkPipeline, VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_pipeline_layout, base, VkPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_render_pass, base, VkRenderPass, VK_OBJECT_TYPE_RENDER_PASS)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_sampler, base, VkSampler, VK_OBJECT_TYPE_SAMPLER)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_shader_module, base, VkShaderModule, VK_OBJECT_TYPE_SHADER_MODULE)
VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_semaphore, base, VkSemaphore, VK_OBJECT_TYPE_SEMAPHORE)

#define panvk_arch_name(name, version) panvk_## version ## _ ## name

#define panvk_arch_dispatch(arch, name, ...) \
do { \
   switch (arch) { \
   case 5: panvk_arch_name(name, v5)(__VA_ARGS__); break; \
   case 6: panvk_arch_name(name, v6)(__VA_ARGS__); break; \
   case 7: panvk_arch_name(name, v7)(__VA_ARGS__); break; \
   default: unreachable("Invalid arch"); \
   } \
} while (0)

#ifdef PAN_ARCH
#if PAN_ARCH == 5
#define panvk_per_arch(name) panvk_arch_name(name, v5)
#elif PAN_ARCH == 6
#define panvk_per_arch(name) panvk_arch_name(name, v6)
#elif PAN_ARCH == 7
#define panvk_per_arch(name) panvk_arch_name(name, v7)
#endif
#include "panvk_vX_cmd_buffer.h"
#include "panvk_vX_cs.h"
#include "panvk_vX_meta.h"
#else
#define PAN_ARCH 5
#define panvk_per_arch(name) panvk_arch_name(name, v5)
#include "panvk_vX_cmd_buffer.h"
#include "panvk_vX_cs.h"
#include "panvk_vX_meta.h"
#undef PAN_ARCH
#undef panvk_per_arch
#define PAN_ARCH 6
#define panvk_per_arch(name) panvk_arch_name(name, v6)
#include "panvk_vX_cmd_buffer.h"
#include "panvk_vX_cs.h"
#include "panvk_vX_meta.h"
#undef PAN_ARCH
#undef panvk_per_arch
#define PAN_ARCH 7
#define panvk_per_arch(name) panvk_arch_name(name, v7)
#include "panvk_vX_cmd_buffer.h"
#include "panvk_vX_cs.h"
#include "panvk_vX_meta.h"
#undef PAN_ARCH
#undef panvk_per_arch
#endif

#ifdef PAN_ARCH
bool
panvk_per_arch(blend_needs_lowering)(const struct panfrost_device *dev,
                                     const struct pan_blend_state *state,
                                     unsigned rt);

struct panvk_shader *
panvk_per_arch(shader_create)(struct panvk_device *dev,
                              gl_shader_stage stage,
                              const VkPipelineShaderStageCreateInfo *stage_info,
                              const struct panvk_pipeline_layout *layout,
                              unsigned sysval_ubo,
                              struct pan_blend_state *blend_state,
                              bool static_blend_constants,
                              const VkAllocationCallbacks *alloc);
#endif

#endif /* PANVK_PRIVATE_H */
