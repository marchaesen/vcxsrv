/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
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

#ifndef PVR_PRIVATE_H
#define PVR_PRIVATE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "compiler/shader_enums.h"
#include "hwdef/rogue_hw_defs.h"
#include "pvr_csb.h"
#include "pvr_device_info.h"
#include "pvr_entrypoints.h"
#include "pvr_hw_pass.h"
#include "pvr_job_render.h"
#include "pvr_limits.h"
#include "pvr_pds.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "rogue/rogue.h"
#include "util/bitscan.h"
#include "util/format/u_format.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "vk_buffer.h"
#include "vk_command_buffer.h"
#include "vk_device.h"
#include "vk_image.h"
#include "vk_instance.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_sync.h"
#include "wsi_common.h"

#ifdef HAVE_VALGRIND
#   include <valgrind/valgrind.h>
#   include <valgrind/memcheck.h>
#   define VG(x) x
#else
#   define VG(x) ((void)0)
#endif

#define VK_VENDOR_ID_IMAGINATION 0x1010

#define PVR_WORKGROUP_DIMENSIONS 3U

#define PVR_SAMPLER_DESCRIPTOR_SIZE 4U
#define PVR_IMAGE_DESCRIPTOR_SIZE 4U

#define PVR_STATE_PBE_DWORDS 2U

#define PVR_PIPELINE_LAYOUT_SUPPORTED_DESCRIPTOR_TYPE_COUNT \
   (uint32_t)(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1U)

/* TODO: move into a common surface library? */
enum pvr_memlayout {
   PVR_MEMLAYOUT_UNDEFINED = 0, /* explicitly treat 0 as undefined */
   PVR_MEMLAYOUT_LINEAR,
   PVR_MEMLAYOUT_TWIDDLED,
   PVR_MEMLAYOUT_3DTWIDDLED,
};

enum pvr_cmd_buffer_status {
   PVR_CMD_BUFFER_STATUS_INVALID = 0, /* explicitly treat 0 as invalid */
   PVR_CMD_BUFFER_STATUS_INITIAL,
   PVR_CMD_BUFFER_STATUS_RECORDING,
   PVR_CMD_BUFFER_STATUS_EXECUTABLE,
};

enum pvr_texture_state {
   PVR_TEXTURE_STATE_SAMPLE,
   PVR_TEXTURE_STATE_STORAGE,
   PVR_TEXTURE_STATE_ATTACHMENT,
   PVR_TEXTURE_STATE_MAX_ENUM,
};

enum pvr_sub_cmd_type {
   PVR_SUB_CMD_TYPE_INVALID = 0, /* explicitly treat 0 as invalid */
   PVR_SUB_CMD_TYPE_GRAPHICS,
   PVR_SUB_CMD_TYPE_COMPUTE,
   PVR_SUB_CMD_TYPE_TRANSFER,
   PVR_SUB_CMD_TYPE_EVENT,
};

enum pvr_event_type {
   PVR_EVENT_TYPE_SET,
   PVR_EVENT_TYPE_RESET,
   PVR_EVENT_TYPE_WAIT,
   PVR_EVENT_TYPE_BARRIER,
};

enum pvr_depth_stencil_usage {
   PVR_DEPTH_STENCIL_USAGE_UNDEFINED = 0, /* explicitly treat 0 as undefined */
   PVR_DEPTH_STENCIL_USAGE_NEEDED,
   PVR_DEPTH_STENCIL_USAGE_NEVER,
};

enum pvr_job_type {
   PVR_JOB_TYPE_GEOM,
   PVR_JOB_TYPE_FRAG,
   PVR_JOB_TYPE_COMPUTE,
   PVR_JOB_TYPE_TRANSFER,
   PVR_JOB_TYPE_MAX
};

enum pvr_pipeline_type {
   PVR_PIPELINE_TYPE_INVALID = 0, /* explicitly treat 0 as undefined */
   PVR_PIPELINE_TYPE_GRAPHICS,
   PVR_PIPELINE_TYPE_COMPUTE,
};

enum pvr_pipeline_stage_bits {
   PVR_PIPELINE_STAGE_GEOM_BIT = BITFIELD_BIT(PVR_JOB_TYPE_GEOM),
   PVR_PIPELINE_STAGE_FRAG_BIT = BITFIELD_BIT(PVR_JOB_TYPE_FRAG),
   PVR_PIPELINE_STAGE_COMPUTE_BIT = BITFIELD_BIT(PVR_JOB_TYPE_COMPUTE),
   PVR_PIPELINE_STAGE_TRANSFER_BIT = BITFIELD_BIT(PVR_JOB_TYPE_TRANSFER),
};

#define PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS \
   (PVR_PIPELINE_STAGE_GEOM_BIT | PVR_PIPELINE_STAGE_FRAG_BIT)

#define PVR_PIPELINE_STAGE_ALL_BITS                                         \
   (PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS | PVR_PIPELINE_STAGE_COMPUTE_BIT | \
    PVR_PIPELINE_STAGE_TRANSFER_BIT)

#define PVR_NUM_SYNC_PIPELINE_STAGES 4U

/* Warning: Do not define an invalid stage as 0 since other code relies on 0
 * being the first shader stage. This allows for stages to be split or added
 * in the future. Defining 0 as invalid will very likely cause problems.
 */
enum pvr_stage_allocation {
   PVR_STAGE_ALLOCATION_VERTEX_GEOMETRY,
   PVR_STAGE_ALLOCATION_FRAGMENT,
   PVR_STAGE_ALLOCATION_COMPUTE,
   PVR_STAGE_ALLOCATION_COUNT
};

/* Scissor accumulation state defines
 *  - Disabled means that a clear has been detected, and scissor accumulation
 *    should stop.
 *  - Check for clear is when there's no clear loadops, but there could be
 *    another clear call that would be broken via scissoring
 *  - Enabled means that a scissor has been set in the pipeline, and
 *    accumulation can continue
 */
enum pvr_scissor_accum_state {
   PVR_SCISSOR_ACCUM_INVALID = 0, /* Explicitly treat 0 as invalid */
   PVR_SCISSOR_ACCUM_DISABLED,
   PVR_SCISSOR_ACCUM_CHECK_FOR_CLEAR,
   PVR_SCISSOR_ACCUM_ENABLED,
};

#define PVR_STATIC_CLEAR_PDS_STATE_COUNT          \
   (pvr_cmd_length(TA_STATE_PDS_SHADERBASE) +     \
    pvr_cmd_length(TA_STATE_PDS_TEXUNICODEBASE) + \
    pvr_cmd_length(TA_STATE_PDS_SIZEINFO1) +      \
    pvr_cmd_length(TA_STATE_PDS_SIZEINFO2) +      \
    pvr_cmd_length(TA_STATE_PDS_VARYINGBASE) +    \
    pvr_cmd_length(TA_STATE_PDS_TEXTUREDATABASE))

/* These can be used as offsets within a PVR_STATIC_CLEAR_PDS_STATE_COUNT dwords
 * sized array to get the respective state word.
 *
 * The values are based on the lengths of the state words.
 */
enum pvr_static_clear_ppp_pds_state_type {
   /* Words enabled by pres_pds_state_ptr0. */
   PVR_STATIC_CLEAR_PPP_PDS_TYPE_SHADERBASE = 0,
   PVR_STATIC_CLEAR_PPP_PDS_TYPE_TEXUNICODEBASE = 1,
   PVR_STATIC_CLEAR_PPP_PDS_TYPE_SIZEINFO1 = 2,
   PVR_STATIC_CLEAR_PPP_PDS_TYPE_SIZEINFO2 = 3,

   /* Word enabled by pres_pds_state_ptr1. */
   PVR_STATIC_CLEAR_PPP_PDS_TYPE_VARYINGBASE = 4,

   /* Word enabled by pres_pds_state_ptr2. */
   PVR_STATIC_CLEAR_PPP_PDS_TYPE_TEXTUREDATABASE = 5,
};

static_assert(PVR_STATIC_CLEAR_PPP_PDS_TYPE_TEXTUREDATABASE + 1 ==
                 PVR_STATIC_CLEAR_PDS_STATE_COUNT,
              "pvr_static_clear_ppp_pds_state_type might require fixing.");

enum pvr_static_clear_variant_bits {
   PVR_STATIC_CLEAR_DEPTH_BIT = BITFIELD_BIT(0),
   PVR_STATIC_CLEAR_STENCIL_BIT = BITFIELD_BIT(1),
   PVR_STATIC_CLEAR_COLOR_BIT = BITFIELD_BIT(2),
};

#define PVR_STATIC_CLEAR_VARIANT_COUNT (PVR_STATIC_CLEAR_COLOR_BIT << 1U)

enum pvr_event_state {
   PVR_EVENT_STATE_SET_BY_HOST,
   PVR_EVENT_STATE_RESET_BY_HOST,
   PVR_EVENT_STATE_SET_BY_DEVICE,
   PVR_EVENT_STATE_RESET_BY_DEVICE
};

struct pvr_bo;
struct pvr_compute_ctx;
struct pvr_compute_pipeline;
struct pvr_free_list;
struct pvr_graphics_pipeline;
struct pvr_instance;
struct pvr_render_ctx;
struct rogue_compiler;

struct pvr_physical_device {
   struct vk_physical_device vk;

   /* Back-pointer to instance */
   struct pvr_instance *instance;

   char *name;
   int master_fd;
   int render_fd;
   char *master_path;
   char *render_path;

   struct pvr_winsys *ws;
   struct pvr_device_info dev_info;

   struct pvr_device_runtime_info dev_runtime_info;

   VkPhysicalDeviceMemoryProperties memory;

   uint8_t pipeline_cache_uuid[VK_UUID_SIZE];

   struct wsi_device wsi_device;

   struct rogue_compiler *compiler;
};

struct pvr_instance {
   struct vk_instance vk;

   int physical_devices_count;
   struct pvr_physical_device physical_device;
};

struct pvr_queue {
   struct vk_queue vk;

   struct pvr_device *device;

   struct pvr_render_ctx *gfx_ctx;
   struct pvr_compute_ctx *compute_ctx;
   struct pvr_transfer_ctx *transfer_ctx;

   struct vk_sync *completion[PVR_JOB_TYPE_MAX];
};

struct pvr_vertex_binding {
   struct pvr_buffer *buffer;
   VkDeviceSize offset;
};

struct pvr_pds_upload {
   struct pvr_bo *pvr_bo;
   /* Offset from the pds heap base address. */
   uint32_t data_offset;
   /* Offset from the pds heap base address. */
   uint32_t code_offset;

   /* data_size + code_size = program_size. */
   uint32_t data_size;
   uint32_t code_size;
};

struct pvr_static_clear_ppp_base {
   uint32_t wclamp;
   uint32_t varying_word[3];
   uint32_t ppp_ctrl;
   uint32_t stream_out0;
};

struct pvr_static_clear_ppp_template {
   /* Pre-packed control words. */
   uint32_t header;
   uint32_t ispb;

   bool requires_pds_state;

   /* Configurable control words.
    * These are initialized and can be modified as needed before emitting them.
    */
   struct {
      struct PVRX(TA_STATE_ISPCTL) ispctl;
      struct PVRX(TA_STATE_ISPA) ispa;

      /* In case the template requires_pds_state this needs to be a valid
       * pointer to a pre-packed PDS state before emitting.
       *
       * Note: this is a pointer to an array of const uint32_t and not an array
       * of pointers or a function pointer.
       */
      const uint32_t (*pds_state)[PVR_STATIC_CLEAR_PDS_STATE_COUNT];

      struct PVRX(TA_REGION_CLIP0) region_clip0;
      struct PVRX(TA_REGION_CLIP1) region_clip1;

      struct PVRX(TA_OUTPUT_SEL) output_sel;
   } config;
};

#define PVR_CLEAR_VDM_STATE_DWORD_COUNT                                        \
   (pvr_cmd_length(VDMCTRL_VDM_STATE0) + pvr_cmd_length(VDMCTRL_VDM_STATE2) +  \
    pvr_cmd_length(VDMCTRL_VDM_STATE3) + pvr_cmd_length(VDMCTRL_VDM_STATE4) +  \
    pvr_cmd_length(VDMCTRL_VDM_STATE5) + pvr_cmd_length(VDMCTRL_INDEX_LIST0) + \
    pvr_cmd_length(VDMCTRL_INDEX_LIST2))

struct pvr_device {
   struct vk_device vk;
   struct pvr_instance *instance;
   struct pvr_physical_device *pdevice;

   int master_fd;
   int render_fd;

   struct pvr_winsys *ws;
   struct pvr_winsys_heaps heaps;

   struct pvr_free_list *global_free_list;

   struct pvr_queue *queues;
   uint32_t queue_count;

   /* Running count of the number of job submissions across all queue. */
   uint32_t global_queue_job_count;

   /* Running count of the number of presentations across all queues. */
   uint32_t global_queue_present_count;

   uint32_t pixel_event_data_size_in_dwords;

   uint64_t input_attachment_sampler;

   struct pvr_pds_upload pds_compute_fence_program;

   struct {
      struct pvr_pds_upload pds;
      struct pvr_bo *usc;
   } nop_program;

   /* Issue Data Fence, Wait for Data Fence state. */
   struct {
      uint32_t usc_shareds;
      struct pvr_bo *usc;

      /* Buffer in which the IDF/WDF program performs store ops. */
      struct pvr_bo *store_bo;
      /* Contains the initialization values for the shared registers. */
      struct pvr_bo *shareds_bo;

      struct pvr_pds_upload pds;
      struct pvr_pds_upload sw_compute_barrier_pds;
   } idfwdf_state;

   struct pvr_device_static_clear_state {
      struct pvr_bo *usc_vertex_shader_bo;
      struct pvr_bo *vertices_bo;
      struct pvr_pds_upload pds;

      struct pvr_static_clear_ppp_base ppp_base;
      struct pvr_static_clear_ppp_template
         ppp_templates[PVR_STATIC_CLEAR_VARIANT_COUNT];

      uint32_t vdm_words[PVR_CLEAR_VDM_STATE_DWORD_COUNT];
      uint32_t large_clear_vdm_words[PVR_CLEAR_VDM_STATE_DWORD_COUNT];
   } static_clear_state;

   VkPhysicalDeviceFeatures features;
};

struct pvr_device_memory {
   struct vk_object_base base;
   struct pvr_winsys_bo *bo;
};

struct pvr_mip_level {
   /* Offset of the mip level in bytes */
   uint32_t offset;

   /* Aligned mip level size in bytes */
   uint32_t size;

   /* Aligned row length in bytes */
   uint32_t pitch;

   /* Aligned height in bytes */
   uint32_t height_pitch;
};

struct pvr_image {
   struct vk_image vk;

   /* vma this image is bound to */
   struct pvr_winsys_vma *vma;

   /* Device address the image is mapped to in device virtual address space */
   pvr_dev_addr_t dev_addr;

   /* Derived and other state */
   VkExtent3D physical_extent;
   enum pvr_memlayout memlayout;
   VkDeviceSize layer_size;
   VkDeviceSize size;

   VkDeviceSize alignment;

   struct pvr_mip_level mip_levels[14];
};

struct pvr_buffer {
   struct vk_buffer vk;

   /* Derived and other state */
   uint32_t alignment;
   /* vma this buffer is bound to */
   struct pvr_winsys_vma *vma;
   /* Device address the buffer is mapped to in device virtual address space */
   pvr_dev_addr_t dev_addr;
};

struct pvr_image_view {
   struct vk_image_view vk;

   /* Prepacked Texture Image dword 0 and 1. It will be copied to the
    * descriptor info during pvr_UpdateDescriptorSets().
    *
    * We create separate texture states for sampling, storage and input
    * attachment cases.
    */
   uint64_t texture_state[PVR_TEXTURE_STATE_MAX_ENUM][2];
};

struct pvr_buffer_view {
   struct vk_object_base base;

   uint64_t range;
   VkFormat format;

   /* Prepacked Texture dword 0 and 1. It will be copied to the descriptor
    * during pvr_UpdateDescriptorSets().
    */
   uint64_t texture_state[2];
};

union pvr_sampler_descriptor {
   uint32_t words[PVR_SAMPLER_DESCRIPTOR_SIZE];

   struct {
      /* Packed PVRX(TEXSTATE_SAMPLER). */
      uint64_t sampler_word;
      uint32_t compare_op;
      /* TODO: Figure out what this word is for and rename.
       * Sampler state word 1?
       */
      uint32_t word3;
   } data;
};

struct pvr_sampler {
   struct vk_object_base base;

   union pvr_sampler_descriptor descriptor;
};

struct pvr_descriptor_size_info {
   /* Non-spillable size for storage in the common store. */
   uint32_t primary;

   /* Spillable size to accommodate limitation of the common store. */
   uint32_t secondary;

   uint32_t alignment;
};

struct pvr_descriptor_set_layout_binding {
   VkDescriptorType type;

   /* "M" in layout(set = N, binding = M)
    * Can be used to index bindings in the descriptor_set_layout. Not the
    * original user specified binding number as those might be non-contiguous.
    */
   uint32_t binding_number;

   uint32_t descriptor_count;

   /* Index into the flattened descriptor set */
   uint16_t descriptor_index;

   VkShaderStageFlags shader_stages;
   /* Mask composed by shifted PVR_STAGE_ALLOCATION_...
    * Makes it easier to check active shader stages by just shifting and
    * ANDing instead of using VkShaderStageFlags and match the PVR_STAGE_...
    */
   uint32_t shader_stage_mask;

   struct {
      uint32_t primary;
      uint32_t secondary;
   } per_stage_offset_in_dwords[PVR_STAGE_ALLOCATION_COUNT];

   bool has_immutable_samplers;
   /* Index at which the samplers can be found in the descriptor_set_layout.
    * 0 when the samplers are at index 0 or no samplers are present.
    */
   uint32_t immutable_samplers_index;
};

/* All sizes are in dwords. */
struct pvr_descriptor_set_layout_mem_layout {
   uint32_t primary_offset;
   uint32_t primary_size;

   uint32_t secondary_offset;
   uint32_t secondary_size;

   uint32_t primary_dynamic_size;
   uint32_t secondary_dynamic_size;
};

struct pvr_descriptor_set_layout {
   struct vk_object_base base;

   /* Total amount of descriptors contained in this set. */
   uint32_t descriptor_count;

   /* Count of dynamic buffers. */
   uint32_t dynamic_buffer_count;

   uint32_t binding_count;
   struct pvr_descriptor_set_layout_binding *bindings;

   uint32_t immutable_sampler_count;
   const struct pvr_sampler **immutable_samplers;

   /* Shader stages requiring access to descriptors in this set. */
   VkShaderStageFlags shader_stages;

   /* Count of each VkDescriptorType per shader stage. Dynamically allocated
    * arrays per stage as to not hard code the max descriptor type here.
    *
    * Note: when adding a new type, it might not numerically follow the
    * previous type so a sparse array will be created. You might want to
    * readjust how these arrays are created and accessed.
    */
   uint32_t *per_stage_descriptor_count[PVR_STAGE_ALLOCATION_COUNT];

   uint32_t total_size_in_dwords;
   struct pvr_descriptor_set_layout_mem_layout
      memory_layout_in_dwords_per_stage[PVR_STAGE_ALLOCATION_COUNT];
};

struct pvr_descriptor_pool {
   struct vk_object_base base;

   VkAllocationCallbacks alloc;

   /* Saved information from pCreateInfo. */
   uint32_t max_sets;

   uint32_t total_size_in_dwords;
   uint32_t current_size_in_dwords;

   /* Derived and other state. */
   /* List of the descriptor sets created using this pool. */
   struct list_head descriptor_sets;
};

struct pvr_descriptor {
   VkDescriptorType type;

   union {
      struct {
         struct pvr_buffer_view *bview;
         pvr_dev_addr_t buffer_dev_addr;
         VkDeviceSize buffer_desc_range;
         VkDeviceSize buffer_create_info_size;
      };

      struct {
         VkImageLayout layout;
         const struct pvr_image_view *iview;
         const struct pvr_sampler *sampler;
      };
   };
};

struct pvr_descriptor_set {
   struct vk_object_base base;

   const struct pvr_descriptor_set_layout *layout;
   const struct pvr_descriptor_pool *pool;

   struct pvr_bo *pvr_bo;

   /* Links this descriptor set into pvr_descriptor_pool::descriptor_sets list.
    */
   struct list_head link;

   /* Array of size layout::descriptor_count. */
   struct pvr_descriptor descriptors[0];
};

struct pvr_event {
   struct vk_object_base base;

   enum pvr_event_state state;
   struct vk_sync *sync;
};

struct pvr_descriptor_state {
   struct pvr_descriptor_set *descriptor_sets[PVR_MAX_DESCRIPTOR_SETS];
   uint32_t valid_mask;
};

struct pvr_transfer_cmd {
   /* Node to link this cmd into the transfer_cmds list in
    * pvr_sub_cmd::transfer structure.
    */
   struct list_head link;

   struct pvr_buffer *src;
   struct pvr_buffer *dst;
   uint32_t region_count;
   VkBufferCopy2 regions[0];
};

struct pvr_sub_cmd_gfx {
   const struct pvr_framebuffer *framebuffer;

   struct pvr_render_job job;

   struct pvr_bo *depth_bias_bo;
   struct pvr_bo *scissor_bo;

   /* Tracking how the loaded depth/stencil values are being used. */
   enum pvr_depth_stencil_usage depth_usage;
   enum pvr_depth_stencil_usage stencil_usage;

   /* Tracking whether the subcommand modifies depth/stencil. */
   bool modifies_depth;
   bool modifies_stencil;

   /* Control stream builder object */
   struct pvr_csb control_stream;

   uint32_t hw_render_idx;

   uint32_t max_tiles_in_flight;

   bool empty_cmd;

   /* True if any fragment shader used in this sub command uses atomic
    * operations.
    */
   bool frag_uses_atomic_ops;

   bool disable_compute_overlap;

   /* True if any fragment shader used in this sub command has side
    * effects.
    */
   bool frag_has_side_effects;

   /* True if any vertex shader used in this sub command contains both
    * texture reads and texture writes.
    */
   bool vertex_uses_texture_rw;

   /* True if any fragment shader used in this sub command contains
    * both texture reads and texture writes.
    */
   bool frag_uses_texture_rw;
};

struct pvr_sub_cmd_compute {
   /* Control stream builder object. */
   struct pvr_csb control_stream;

   struct pvr_winsys_compute_submit_info submit_info;

   uint32_t num_shared_regs;

   /* True if any shader used in this sub command uses atomic
    * operations.
    */
   bool uses_atomic_ops;

   bool uses_barrier;

   bool pds_sw_barrier_requires_clearing;
};

struct pvr_sub_cmd_transfer {
   /* List of pvr_transfer_cmd type structures. */
   struct list_head transfer_cmds;
};

struct pvr_sub_cmd_event {
   enum pvr_event_type type;

   union {
      struct {
         struct pvr_event *event;
         /* Stages to wait for until the event is set. */
         uint32_t wait_for_stage_mask;
      } set;

      struct {
         struct pvr_event *event;
         /* Stages to wait for until the event is reset. */
         uint32_t wait_for_stage_mask;
      } reset;

      struct {
         uint32_t count;
         /* Events to wait for before resuming. */
         struct pvr_event **events;
         /* Stages to wait at. */
         uint32_t *wait_at_stage_masks;
      } wait;
   };
};

struct pvr_sub_cmd {
   /* This links the subcommand in pvr_cmd_buffer:sub_cmds list. */
   struct list_head link;

   enum pvr_sub_cmd_type type;

   union {
      struct pvr_sub_cmd_gfx gfx;
      struct pvr_sub_cmd_compute compute;
      struct pvr_sub_cmd_transfer transfer;
      struct pvr_sub_cmd_event event;
   };
};

struct pvr_render_pass_info {
   const struct pvr_render_pass *pass;
   struct pvr_framebuffer *framebuffer;

   struct pvr_image_view **attachments;

   uint32_t subpass_idx;
   uint32_t current_hw_subpass;

   VkRect2D render_area;

   uint32_t clear_value_count;
   VkClearValue *clear_values;

   VkPipelineBindPoint pipeline_bind_point;

   bool process_empty_tiles;
   bool enable_bg_tag;
   uint32_t userpass_spawn;

   /* Have we had to scissor a depth/stencil clear because render area was not
    * tile aligned?
    */
   bool scissor_ds_clear;
};

struct pvr_emit_state {
   bool ppp_control : 1;
   bool isp : 1;
   bool isp_fb : 1;
   bool isp_ba : 1;
   bool isp_bb : 1;
   bool isp_dbsc : 1;
   bool pds_fragment_stateptr0 : 1;
   bool pds_fragment_stateptr1 : 1;
   bool pds_fragment_stateptr2 : 1;
   bool pds_fragment_stateptr3 : 1;
   bool region_clip : 1;
   bool viewport : 1;
   bool wclamp : 1;
   bool output_selects : 1;
   bool varying_word0 : 1;
   bool varying_word1 : 1;
   bool varying_word2 : 1;
   bool stream_out : 1;
};

struct pvr_ppp_state {
   uint32_t header;

   struct {
      /* TODO: Can we get rid of the "control" field? */
      struct PVRX(TA_STATE_ISPCTL) control_struct;
      uint32_t control;

      uint32_t front_a;
      uint32_t front_b;
      uint32_t back_a;
      uint32_t back_b;
   } isp;

   struct {
      uint16_t scissor_index;
      uint16_t depthbias_index;
   } depthbias_scissor_indices;

   struct {
      uint32_t pixel_shader_base;
      uint32_t texture_uniform_code_base;
      uint32_t size_info1;
      uint32_t size_info2;
      uint32_t varying_base;
      uint32_t texture_state_data_base;
      uint32_t uniform_state_data_base;
   } pds;

   struct {
      uint32_t word0;
      uint32_t word1;
   } region_clipping;

   struct {
      uint32_t a0;
      uint32_t m0;
      uint32_t a1;
      uint32_t m1;
      uint32_t a2;
      uint32_t m2;
   } viewports[PVR_MAX_VIEWPORTS];

   uint32_t viewport_count;

   uint32_t output_selects;

   uint32_t varying_word[2];

   uint32_t ppp_control;
};

#define PVR_DYNAMIC_STATE_BIT_VIEWPORT BITFIELD_BIT(0U)
#define PVR_DYNAMIC_STATE_BIT_SCISSOR BITFIELD_BIT(1U)
#define PVR_DYNAMIC_STATE_BIT_LINE_WIDTH BITFIELD_BIT(2U)
#define PVR_DYNAMIC_STATE_BIT_DEPTH_BIAS BITFIELD_BIT(3U)
#define PVR_DYNAMIC_STATE_BIT_STENCIL_COMPARE_MASK BITFIELD_BIT(4U)
#define PVR_DYNAMIC_STATE_BIT_STENCIL_WRITE_MASK BITFIELD_BIT(5U)
#define PVR_DYNAMIC_STATE_BIT_STENCIL_REFERENCE BITFIELD_BIT(6U)
#define PVR_DYNAMIC_STATE_BIT_BLEND_CONSTANTS BITFIELD_BIT(7U)

#define PVR_DYNAMIC_STATE_ALL_BITS \
   ((PVR_DYNAMIC_STATE_BIT_BLEND_CONSTANTS << 1U) - 1U)

struct pvr_dynamic_state {
   /* Identifies which pipeline state is static or dynamic.
    * To test for dynamic: & PVR_STATE_BITS_...
    */
   uint32_t mask;

   struct {
      /* TODO: fixme in the original code - figure out what. */
      uint32_t count;
      VkViewport viewports[PVR_MAX_VIEWPORTS];
   } viewport;

   struct {
      /* TODO: fixme in the original code - figure out what. */
      uint32_t count;
      VkRect2D scissors[PVR_MAX_VIEWPORTS];
   } scissor;

   /* Saved information from pCreateInfo. */
   float line_width;

   /* Do not change this. This is the format used for the depth_bias_array
    * elements uploaded to the device.
    */
   struct pvr_depth_bias_state {
      /* Saved information from pCreateInfo. */
      float constant_factor;
      float slope_factor;
      float clamp;
   } depth_bias;
   float blend_constants[4];
   struct {
      uint32_t front;
      uint32_t back;
   } compare_mask;
   struct {
      uint32_t front;
      uint32_t back;
   } write_mask;
   struct {
      uint32_t front;
      uint32_t back;
   } reference;
};

struct pvr_cmd_buffer_draw_state {
   uint32_t base_instance;
   uint32_t base_vertex;
   bool draw_indirect;
   bool draw_indexed;
};

struct pvr_cmd_buffer_state {
   VkResult status;

   /* Pipeline binding. */
   const struct pvr_graphics_pipeline *gfx_pipeline;

   const struct pvr_compute_pipeline *compute_pipeline;

   struct pvr_render_pass_info render_pass_info;

   struct pvr_sub_cmd *current_sub_cmd;

   struct pvr_ppp_state ppp_state;

   union {
      struct pvr_emit_state emit_state;
      /* This is intended to allow setting and clearing of all bits. This
       * shouldn't be used to access specific bits of ppp_state.
       */
      uint32_t emit_state_bits;
   };

   struct {
      /* FIXME: Check if we need a dirty state flag for the given scissor
       * accumulation state.
       * Check whether these members should be moved in the top level struct
       * and this struct replaces with just pvr_dynamic_state "dynamic".
       */
      enum pvr_scissor_accum_state scissor_accum_state;
      VkRect2D scissor_accum_bounds;

      struct pvr_dynamic_state common;
   } dynamic;

   struct pvr_vertex_binding vertex_bindings[PVR_MAX_VERTEX_INPUT_BINDINGS];

   struct {
      struct pvr_buffer *buffer;
      VkDeviceSize offset;
      VkIndexType type;
   } index_buffer_binding;

   struct {
      uint8_t data[PVR_MAX_PUSH_CONSTANTS_SIZE];
      VkShaderStageFlags dirty_stages;
   } push_constants;

   /* Array size of barriers_needed is based on number of sync pipeline
    * stages.
    */
   uint32_t barriers_needed[4];

   struct pvr_descriptor_state gfx_desc_state;
   struct pvr_descriptor_state compute_desc_state;

   VkFormat depth_format;

   struct {
      bool viewport : 1;
      bool scissor : 1;

      bool compute_pipeline_binding : 1;
      bool compute_desc_dirty : 1;

      bool gfx_pipeline_binding : 1;
      bool gfx_desc_dirty : 1;

      bool vertex_bindings : 1;
      bool index_buffer_binding : 1;
      bool vertex_descriptors : 1;
      bool fragment_descriptors : 1;

      bool line_width : 1;

      bool depth_bias : 1;

      bool blend_constants : 1;

      bool compare_mask : 1;
      bool write_mask : 1;
      bool reference : 1;

      bool userpass_spawn : 1;

      /* Some draw state needs to be tracked for changes between draw calls
       * i.e. if we get a draw with baseInstance=0, followed by a call with
       * baseInstance=1 that needs to cause us to select a different PDS
       * attrib program and update the BASE_INSTANCE PDS const. If only
       * baseInstance changes then we just have to update the data section.
       */
      bool draw_base_instance : 1;
      bool draw_variant : 1;
   } dirty;

   struct pvr_cmd_buffer_draw_state draw_state;

   struct {
      uint32_t code_offset;
      const struct pvr_pds_info *info;
   } pds_shader;

   uint32_t max_shared_regs;

   /* Address of data segment for vertex attrib upload program. */
   uint32_t pds_vertex_attrib_offset;

   uint32_t pds_fragment_descriptor_data_offset;
   uint32_t pds_compute_descriptor_data_offset;
};

static_assert(
   sizeof(((struct pvr_cmd_buffer_state *)(0))->emit_state) <=
      sizeof(((struct pvr_cmd_buffer_state *)(0))->emit_state_bits),
   "Size of emit_state_bits must be greater that or equal to emit_state.");

struct pvr_cmd_buffer {
   struct vk_command_buffer vk;

   struct pvr_device *device;

   /* Buffer status, invalid/initial/recording/executable */
   enum pvr_cmd_buffer_status status;

   /* Buffer usage flags */
   VkCommandBufferUsageFlags usage_flags;

   struct util_dynarray depth_bias_array;

   struct util_dynarray scissor_array;
   uint32_t scissor_words[2];

   struct pvr_cmd_buffer_state state;

   /* List of pvr_bo structs associated with this cmd buffer. */
   struct list_head bo_list;

   struct list_head sub_cmds;
};

struct pvr_pipeline_layout {
   struct vk_object_base base;

   uint32_t set_count;
   /* Contains set_count amount of descriptor set layouts. */
   struct pvr_descriptor_set_layout *set_layout[PVR_MAX_DESCRIPTOR_SETS];

   VkShaderStageFlags push_constants_shader_stages;

   VkShaderStageFlags shader_stages;

   /* Per stage masks indicating which set in the layout contains any
    * descriptor of the appropriate types: VK..._{SAMPLER, SAMPLED_IMAGE,
    * UNIFORM_TEXEL_BUFFER, UNIFORM_BUFFER, STORAGE_BUFFER}.
    * Shift by the set's number to check the mask (1U << set_num).
    */
   uint32_t per_stage_descriptor_masks[PVR_STAGE_ALLOCATION_COUNT];

   /* Array of descriptor offsets at which the set's descriptors' start, per
    * stage, within all the sets in the pipeline layout per descriptor type.
    * Note that we only store into for specific descriptor types
    * VK_DESCRIPTOR_TYPE_{SAMPLER, SAMPLED_IMAGE, UNIFORM_TEXEL_BUFFER,
    * UNIFORM_BUFFER, STORAGE_BUFFER}, the rest will be 0.
    */
   uint32_t
      descriptor_offsets[PVR_MAX_DESCRIPTOR_SETS][PVR_STAGE_ALLOCATION_COUNT]
                        [PVR_PIPELINE_LAYOUT_SUPPORTED_DESCRIPTOR_TYPE_COUNT];

   /* There is no accounting for dynamics in here. They will be garbage values.
    */
   struct pvr_descriptor_set_layout_mem_layout
      register_layout_in_dwords_per_stage[PVR_STAGE_ALLOCATION_COUNT]
                                         [PVR_MAX_DESCRIPTOR_SETS];

   /* All sizes in dwords. */
   struct pvr_pipeline_layout_reg_info {
      uint32_t primary_dynamic_size_in_dwords;
      uint32_t secondary_dynamic_size_in_dwords;
   } per_stage_reg_info[PVR_STAGE_ALLOCATION_COUNT];
};

struct pvr_pipeline_cache {
   struct vk_object_base base;

   struct pvr_device *device;
};

struct pvr_stage_allocation_descriptor_state {
   struct pvr_pds_upload pds_code;
   /* Since we upload the code segment separately from the data segment
    * pds_code->data_size might be 0 whilst
    * pds_info->data_size_in_dwords might be >0 in the case of this struct
    * referring to the code upload.
    */
   struct pvr_pds_info pds_info;

   /* Already setup compile time static consts. */
   struct pvr_bo *static_consts;
};

struct pvr_pds_attrib_program {
   struct pvr_pds_info info;
   /* The uploaded PDS program stored here only contains the code segment,
    * meaning the data size will be 0, unlike the data size stored in the
    * 'info' member above.
    */
   struct pvr_pds_upload program;
};

struct pvr_pipeline_stage_state {
   uint32_t const_shared_reg_count;
   uint32_t const_shared_reg_offset;
   uint32_t temps_count;

   uint32_t coefficient_size;

   /* True if this shader uses any atomic operations. */
   bool uses_atomic_ops;

   /* True if this shader uses both texture reads and texture writes. */
   bool uses_texture_rw;

   /* Only used for compute stage. */
   bool uses_barrier;

   /* True if this shader has side effects */
   bool has_side_effects;

   /* True if this shader is simply a nop.end. */
   bool empty_program;
};

struct pvr_vertex_shader_state {
   /* Pointer to a buffer object that contains the shader binary. */
   struct pvr_bo *bo;
   uint32_t entry_offset;

   /* 2 since we only need STATE_VARYING{0,1} state words. */
   uint32_t varying[2];

   struct pvr_pds_attrib_program
      pds_attrib_programs[PVR_PDS_VERTEX_ATTRIB_PROGRAM_COUNT];

   struct pvr_pipeline_stage_state stage_state;
   /* FIXME: Move this into stage_state? */
   struct pvr_stage_allocation_descriptor_state descriptor_state;
   uint32_t vertex_input_size;
   uint32_t vertex_output_size;
   uint32_t user_clip_planes_mask;
};

struct pvr_fragment_shader_state {
   /* Pointer to a buffer object that contains the shader binary. */
   struct pvr_bo *bo;
   uint32_t entry_offset;

   struct pvr_pipeline_stage_state stage_state;
   /* FIXME: Move this into stage_state? */
   struct pvr_stage_allocation_descriptor_state descriptor_state;
   uint32_t pass_type;

   struct pvr_pds_upload pds_coeff_program;
   struct pvr_pds_upload pds_fragment_program;
};

struct pvr_pipeline {
   struct vk_object_base base;

   enum pvr_pipeline_type type;

   /* Saved information from pCreateInfo. */
   struct pvr_pipeline_layout *layout;
};

struct pvr_compute_pipeline {
   struct pvr_pipeline base;

   struct {
      /* TODO: Change this to be an anonymous struct once the shader hardcoding
       * is removed.
       */
      struct pvr_compute_pipeline_shader_state {
         /* Pointer to a buffer object that contains the shader binary. */
         struct pvr_bo *bo;

         bool uses_atomic_ops;
         bool uses_barrier;
         /* E.g. GLSL shader uses gl_NumWorkGroups. */
         bool uses_num_workgroups;

         uint32_t const_shared_reg_count;
         uint32_t input_register_count;
         uint32_t work_size;
         uint32_t coefficient_register_count;
      } shader;

      struct {
         uint32_t base_workgroup : 1;
      } flags;

      struct pvr_stage_allocation_descriptor_state descriptor;

      struct pvr_pds_upload primary_program;
      struct pvr_pds_info primary_program_info;

      struct pvr_pds_base_workgroup_program {
         struct pvr_pds_upload code_upload;

         uint32_t *data_section;
         /* Offset within the PDS data section at which the base workgroup id
          * resides.
          */
         uint32_t base_workgroup_data_patching_offset;

         struct pvr_pds_info info;
      } primary_base_workgroup_variant_program;
   } state;
};

struct pvr_graphics_pipeline {
   struct pvr_pipeline base;

   VkSampleCountFlagBits rasterization_samples;
   struct pvr_raster_state {
      /* Derived and other state. */
      /* Indicates whether primitives are discarded immediately before the
       * rasterization stage.
       */
      bool discard_enable;
      VkCullModeFlags cull_mode;
      VkFrontFace front_face;
      bool depth_bias_enable;
      bool depth_clamp_enable;
   } raster_state;
   struct {
      VkPrimitiveTopology topology;
      bool primitive_restart;
   } input_asm_state;
   uint32_t sample_mask;

   struct pvr_dynamic_state dynamic_state;

   VkCompareOp depth_compare_op;
   bool depth_write_disable;

   struct {
      VkCompareOp compare_op;
      /* SOP1 */
      VkStencilOp fail_op;
      /* SOP2 */
      VkStencilOp depth_fail_op;
      /* SOP3 */
      VkStencilOp pass_op;
   } stencil_front, stencil_back;

   /* Derived and other state */
   size_t stage_indices[MESA_SHADER_FRAGMENT + 1];

   struct pvr_vertex_shader_state vertex_shader_state;
   struct pvr_fragment_shader_state fragment_shader_state;
};

struct pvr_query_pool {
   struct vk_object_base base;

   /* Stride of result_buffer to get to the start of the results for the next
    * Phantom.
    */
   uint32_t result_stride;

   struct pvr_bo *result_buffer;
   struct pvr_bo *availability_buffer;
};

struct pvr_render_target {
   struct pvr_rt_dataset *rt_dataset;

   pthread_mutex_t mutex;

   bool valid;
};

struct pvr_framebuffer {
   struct vk_object_base base;

   /* Saved information from pCreateInfo. */
   uint32_t width;
   uint32_t height;
   uint32_t layers;

   uint32_t attachment_count;
   struct pvr_image_view **attachments;

   /* Derived and other state. */
   struct pvr_bo *ppp_state_bo;
   /* PPP state size in dwords. */
   size_t ppp_state_size;

   uint32_t render_targets_count;
   struct pvr_render_target *render_targets;
};

struct pvr_render_pass_attachment {
   /* Saved information from pCreateInfo. */
   VkAttachmentLoadOp load_op;

   VkAttachmentStoreOp store_op;

   VkAttachmentLoadOp stencil_load_op;

   VkAttachmentStoreOp stencil_store_op;

   VkFormat vk_format;
   uint32_t sample_count;
   VkImageLayout initial_layout;

   /*  Derived and other state. */
   /* True if the attachment format includes a stencil component. */
   bool has_stencil;

   /* Can this surface be resolved by the PBE. */
   bool is_pbe_downscalable;

   uint32_t index;
};

struct pvr_render_subpass {
   /* Saved information from pCreateInfo. */
   /* The number of samples per color attachment (or depth attachment if
    * z-only).
    */
   /* FIXME: rename to 'samples' to match struct pvr_image */
   uint32_t sample_count;

   uint32_t color_count;
   uint32_t *color_attachments;
   uint32_t *resolve_attachments;

   uint32_t input_count;
   uint32_t *input_attachments;

   uint32_t *depth_stencil_attachment;

   /*  Derived and other state. */
   uint32_t dep_count;
   uint32_t *dep_list;

   /* Array with dep_count elements. flush_on_dep[x] is true if this subpass
    * and the subpass dep_list[x] can't be in the same hardware render.
    */
   bool *flush_on_dep;

   uint32_t index;

   uint32_t userpass_spawn;

   VkPipelineBindPoint pipeline_bind_point;
};

struct pvr_render_pass {
   struct vk_object_base base;

   /* Saved information from pCreateInfo. */
   uint32_t attachment_count;

   struct pvr_render_pass_attachment *attachments;

   uint32_t subpass_count;

   struct pvr_render_subpass *subpasses;

   struct pvr_renderpass_hwsetup *hw_setup;

   /*  Derived and other state. */
   /* FIXME: rename to 'max_samples' as we use 'samples' elsewhere */
   uint32_t max_sample_count;

   /* The maximum number of tile buffers to use in any subpass. */
   uint32_t max_tilebuffer_count;
};

struct pvr_load_op {
   bool is_hw_object;

   uint32_t clear_mask;

   struct pvr_bo *usc_frag_prog_bo;
   uint32_t const_shareds_count;
   uint32_t shareds_dest_offset;
   uint32_t shareds_count;

   struct pvr_pds_upload pds_frag_prog;

   struct pvr_pds_upload pds_tex_state_prog;
   uint32_t temps_count;
};

uint32_t pvr_calc_fscommon_size_and_tiles_in_flight(
   const struct pvr_physical_device *pdevice,
   uint32_t fs_common_size,
   uint32_t min_tiles_in_flight);

VkResult pvr_wsi_init(struct pvr_physical_device *pdevice);
void pvr_wsi_finish(struct pvr_physical_device *pdevice);

VkResult pvr_queues_create(struct pvr_device *device,
                           const VkDeviceCreateInfo *pCreateInfo);
void pvr_queues_destroy(struct pvr_device *device);

VkResult pvr_bind_memory(struct pvr_device *device,
                         struct pvr_device_memory *mem,
                         VkDeviceSize offset,
                         VkDeviceSize size,
                         VkDeviceSize alignment,
                         struct pvr_winsys_vma **const vma_out,
                         pvr_dev_addr_t *const dev_addr_out);
void pvr_unbind_memory(struct pvr_device *device, struct pvr_winsys_vma *vma);

VkResult pvr_gpu_upload(struct pvr_device *device,
                        struct pvr_winsys_heap *heap,
                        const void *data,
                        size_t size,
                        uint64_t alignment,
                        struct pvr_bo **const pvr_bo_out);
VkResult pvr_gpu_upload_pds(struct pvr_device *device,
                            const uint32_t *data,
                            uint32_t data_size_dwords,
                            uint32_t data_alignment,
                            const uint32_t *code,
                            uint32_t code_size_dwords,
                            uint32_t code_alignment,
                            uint64_t min_alignment,
                            struct pvr_pds_upload *const pds_upload_out);

VkResult pvr_gpu_upload_usc(struct pvr_device *device,
                            const void *code,
                            size_t code_size,
                            uint64_t code_alignment,
                            struct pvr_bo **const pvr_bo_out);

VkResult pvr_cmd_buffer_add_transfer_cmd(struct pvr_cmd_buffer *cmd_buffer,
                                         struct pvr_transfer_cmd *transfer_cmd);

VkResult pvr_cmd_buffer_alloc_mem(struct pvr_cmd_buffer *cmd_buffer,
                                  struct pvr_winsys_heap *heap,
                                  uint64_t size,
                                  uint32_t flags,
                                  struct pvr_bo **const pvr_bo_out);

void pvr_calculate_vertex_cam_size(const struct pvr_device_info *dev_info,
                                   const uint32_t vs_output_size,
                                   const bool raster_enable,
                                   uint32_t *const cam_size_out,
                                   uint32_t *const vs_max_instances_out);

VkResult pvr_emit_ppp_from_template(
   struct pvr_csb *const csb,
   const struct pvr_static_clear_ppp_template *const template,
   struct pvr_bo **const pvr_bo_out);

static inline struct pvr_compute_pipeline *
to_pvr_compute_pipeline(struct pvr_pipeline *pipeline)
{
   assert(pipeline->type == PVR_PIPELINE_TYPE_COMPUTE);
   return container_of(pipeline, struct pvr_compute_pipeline, base);
}

static inline struct pvr_graphics_pipeline *
to_pvr_graphics_pipeline(struct pvr_pipeline *pipeline)
{
   assert(pipeline->type == PVR_PIPELINE_TYPE_GRAPHICS);
   return container_of(pipeline, struct pvr_graphics_pipeline, base);
}

static inline const struct pvr_image *
vk_to_pvr_image(const struct vk_image *image)
{
   return container_of(image, const struct pvr_image, vk);
}

static enum pvr_pipeline_stage_bits
pvr_stage_mask(VkPipelineStageFlags2 stage_mask)
{
   enum pvr_pipeline_stage_bits stages = 0;

   if (stage_mask & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
      return PVR_PIPELINE_STAGE_ALL_BITS;

   if (stage_mask & (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT))
      stages |= PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS;

   if (stage_mask & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                     VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                     VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                     VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT)) {
      stages |= PVR_PIPELINE_STAGE_GEOM_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)) {
      stages |= PVR_PIPELINE_STAGE_FRAG_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)) {
      stages |= PVR_PIPELINE_STAGE_COMPUTE_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_TRANSFER_BIT))
      stages |= PVR_PIPELINE_STAGE_TRANSFER_BIT;

   return stages;
}

static inline enum pvr_pipeline_stage_bits
pvr_stage_mask_src(VkPipelineStageFlags2KHR stage_mask)
{
   /* If the source is bottom of pipe, all stages will need to be waited for. */
   if (stage_mask & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
      return PVR_PIPELINE_STAGE_ALL_BITS;

   return pvr_stage_mask(stage_mask);
}

static inline enum pvr_pipeline_stage_bits
pvr_stage_mask_dst(VkPipelineStageFlags2KHR stage_mask)
{
   /* If the destination is top of pipe, all stages should be blocked by prior
    * commands.
    */
   if (stage_mask & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
      return PVR_PIPELINE_STAGE_ALL_BITS;

   return pvr_stage_mask(stage_mask);
}

VkResult pvr_pds_fragment_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   const struct pvr_bo *fragment_shader_bo,
   uint32_t fragment_temp_count,
   enum rogue_msaa_mode msaa_mode,
   bool has_phase_rate_change,
   struct pvr_pds_upload *const pds_upload_out);

VkResult pvr_pds_unitex_state_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   uint32_t texture_kicks,
   uint32_t uniform_kicks,
   struct pvr_pds_upload *const pds_upload_out);

#define PVR_FROM_HANDLE(__pvr_type, __name, __handle) \
   VK_FROM_HANDLE(__pvr_type, __name, __handle)

VK_DEFINE_HANDLE_CASTS(pvr_cmd_buffer,
                       vk.base,
                       VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)
VK_DEFINE_HANDLE_CASTS(pvr_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(pvr_instance,
                       vk.base,
                       VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(pvr_physical_device,
                       vk.base,
                       VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(pvr_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_device_memory,
                               base,
                               VkDeviceMemory,
                               VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_pipeline_cache,
                               base,
                               VkPipelineCache,
                               VK_OBJECT_TYPE_PIPELINE_CACHE)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_buffer,
                               vk.base,
                               VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_image_view,
                               vk.base,
                               VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_buffer_view,
                               base,
                               VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_descriptor_set_layout,
                               base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_descriptor_set,
                               base,
                               VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_descriptor_pool,
                               base,
                               VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_sampler,
                               base,
                               VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_pipeline_layout,
                               base,
                               VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_pipeline,
                               base,
                               VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_query_pool,
                               base,
                               VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_framebuffer,
                               base,
                               VkFramebuffer,
                               VK_OBJECT_TYPE_FRAMEBUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_render_pass,
                               base,
                               VkRenderPass,
                               VK_OBJECT_TYPE_RENDER_PASS)

/**
 * Warn on ignored extension structs.
 *
 * The Vulkan spec requires us to ignore unsupported or unknown structs in
 * a pNext chain. In debug mode, emitting warnings for ignored structs may
 * help us discover structs that we should not have ignored.
 *
 *
 * From the Vulkan 1.0.38 spec:
 *
 *    Any component of the implementation (the loader, any enabled layers,
 *    and drivers) must skip over, without processing (other than reading the
 *    sType and pNext members) any chained structures with sType values not
 *    defined by extensions supported by that component.
 */
#define pvr_debug_ignored_stype(sType) \
   mesa_logd("%s: ignored VkStructureType %u\n", __func__, (sType))

/* Debug helper macros. */
#define PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer)         \
   do {                                                             \
      struct pvr_cmd_buffer *const _cmd_buffer = (cmd_buffer);      \
      if (_cmd_buffer->status != PVR_CMD_BUFFER_STATUS_RECORDING) { \
         vk_errorf(_cmd_buffer,                                     \
                   VK_ERROR_OUT_OF_DEVICE_MEMORY,                   \
                   "Command buffer is not in recording state");     \
         return;                                                    \
      } else if (_cmd_buffer->state.status < VK_SUCCESS) {          \
         vk_errorf(_cmd_buffer,                                     \
                   _cmd_buffer->state.status,                       \
                   "Skipping function as command buffer has "       \
                   "previous build error");                         \
         return;                                                    \
      }                                                             \
   } while (0)

/**
 * Print a FINISHME message, including its source location.
 */
#define pvr_finishme(format, ...)              \
   do {                                        \
      static bool reported = false;            \
      if (!reported) {                         \
         mesa_logw("%s:%d: FINISHME: " format, \
                   __FILE__,                   \
                   __LINE__,                   \
                   ##__VA_ARGS__);             \
         reported = true;                      \
      }                                        \
   } while (false)

/* A non-fatal assert. Useful for debugging. */
#ifdef DEBUG
#   define pvr_assert(x)                                           \
      ({                                                           \
         if (unlikely(!(x)))                                       \
            mesa_loge("%s:%d ASSERT: %s", __FILE__, __LINE__, #x); \
      })
#else
#   define pvr_assert(x)
#endif

#endif /* PVR_PRIVATE_H */
