/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/macros.h"

#include "util/list.h"
#include "agx_helpers.h"
#include "agx_linker.h"
#include "agx_pack.h"
#include "agx_tilebuffer.h"
#include "agx_uvs.h"
#include "pool.h"
#include "shader_enums.h"

#include "hk_private.h"
#include "hk_shader.h"

#include "hk_cmd_pool.h"
#include "hk_descriptor_set.h"

#include "asahi/lib/agx_nir_lower_vbo.h"
#include "util/u_dynarray.h"
#include "vulkan/vulkan_core.h"

#include "vk_command_buffer.h"

#include <stdio.h>

struct hk_buffer;
struct hk_cmd_bo;
struct hk_cmd_pool;
struct hk_image_view;
struct hk_push_descriptor_set;
struct hk_shader;
struct hk_linked_shader;
struct agx_usc_builder;
struct vk_shader;

/** Root descriptor table. */
struct hk_root_descriptor_table {
   uint64_t root_desc_addr;

   union {
      struct {
         uint32_t view_index;
         uint32_t ppp_multisamplectl;

         /* Vertex input state */
         uint64_t attrib_base[AGX_MAX_VBUFS];
         uint32_t attrib_clamps[AGX_MAX_VBUFS];

         /* Pointer to the VS->TCS, VS->GS, or TES->GS buffer. */
         uint64_t vertex_output_buffer;

         /* Mask of outputs flowing VS->TCS, VS->GS, or TES->GS . */
         uint64_t vertex_outputs;

         /* Address of input assembly buffer if geom/tess is used, else 0 */
         uint64_t input_assembly;

         /* Address of tessellation param buffer if tessellation used, else 0 */
         uint64_t tess_params;

         /* Address of geometry param buffer if GS is used, else 0 */
         uint64_t geometry_params;

         /* Pipeline statistics queries. This is a base address with flags. */
         uint64_t pipeline_stats;
         VkQueryPipelineStatisticFlags pipeline_stats_flags;

         float blend_constant[4];
         uint16_t no_epilog_discard;
         uint16_t _pad1;
         uint16_t api_sample_mask;
         uint16_t _pad2;
         uint16_t force_never_in_shader;
         uint16_t _pad3;
         uint16_t provoking;
         uint16_t _pad4;

         /* Mapping from varying slots written by the last vertex stage to UVS
          * indices. This mapping must be compatible with the fragment shader.
          */
         uint8_t uvs_index[VARYING_SLOT_MAX];
      } draw;
      struct {
         uint64_t group_count_addr;
         uint32_t base_group[3];
      } cs;
   };

   /* Client push constants */
   uint8_t push[HK_MAX_PUSH_SIZE];

   /* Descriptor set base addresses */
   uint64_t sets[HK_MAX_SETS];

   /* Dynamic buffer bindings */
   struct hk_buffer_address dynamic_buffers[HK_MAX_DYNAMIC_BUFFERS];

   /* Start index in dynamic_buffers where each set starts */
   uint8_t set_dynamic_buffer_start[HK_MAX_SETS];
};

/* helper macro for computing root descriptor byte offsets */
#define hk_root_descriptor_offset(member)                                      \
   offsetof(struct hk_root_descriptor_table, member)

struct hk_descriptor_state {
   bool root_dirty;
   struct hk_root_descriptor_table root;

   uint32_t set_sizes[HK_MAX_SETS];
   struct hk_descriptor_set *sets[HK_MAX_SETS];
   uint32_t sets_dirty;

   struct hk_push_descriptor_set *push[HK_MAX_SETS];
   uint32_t push_dirty;
};

struct hk_attachment {
   VkFormat vk_format;
   struct hk_image_view *iview;

   VkResolveModeFlagBits resolve_mode;
   struct hk_image_view *resolve_iview;
};

struct hk_bg_eot {
   uint64_t usc;
   struct agx_counts_packed counts;
};

struct hk_render_registers {
   uint32_t width, height, layers;
   uint32_t isp_bgobjdepth;
   uint32_t isp_bgobjvals;
   struct agx_zls_control_packed zls_control, zls_control_partial;
   uint32_t iogpu_unk_214;
   uint32_t depth_dimensions;
   bool process_empty_tiles;

   struct {
      uint32_t dimensions;
      uint64_t buffer, meta;
      uint32_t stride, meta_stride;
   } depth;

   struct {
      uint64_t buffer, meta;
      uint32_t stride, meta_stride;
   } stencil;

   struct {
      struct hk_bg_eot main;
      struct hk_bg_eot partial;
   } bg;

   struct {
      struct hk_bg_eot main;
      struct hk_bg_eot partial;
   } eot;
};

struct hk_rendering_state {
   VkRenderingFlagBits flags;

   VkRect2D area;
   uint32_t layer_count;
   uint32_t view_mask;

   uint32_t color_att_count;
   struct hk_attachment color_att[HK_MAX_RTS];
   struct hk_attachment depth_att;
   struct hk_attachment stencil_att;

   struct agx_tilebuffer_layout tilebuffer;
   struct hk_render_registers cr;
};

struct hk_index_buffer_state {
   struct hk_addr_range buffer;
   enum agx_index_size size;
   uint32_t restart;
};

/* Dirty tracking bits for state not tracked by vk_dynamic_graphics_state or
 * shaders_dirty.
 */
enum hk_dirty {
   HK_DIRTY_INDEX = BITFIELD_BIT(0),
   HK_DIRTY_VB = BITFIELD_BIT(1),
   HK_DIRTY_OCCLUSION = BITFIELD_BIT(2),
   HK_DIRTY_PROVOKING = BITFIELD_BIT(3),
   HK_DIRTY_VARYINGS = BITFIELD_BIT(4),
};

struct hk_graphics_state {
   struct hk_rendering_state render;
   struct hk_descriptor_state descriptors;

   enum hk_dirty dirty;

   uint64_t root;
   uint64_t draw_params;
   uint64_t draw_id_ptr;

   uint32_t shaders_dirty;
   struct hk_api_shader *shaders[MESA_SHADER_MESH + 1];

   /* Vertex buffers */
   struct hk_addr_range vb[AGX_MAX_VBUFS];

   /* Transform feedback buffers */
   struct hk_addr_range xfb[4];

   /* Is transform feedback enabled? */
   bool xfb_enabled;

   /* Internal transform feedback offset vec4.
    *
    * TODO: Strictly could be global.
    */
   uint64_t xfb_offsets;

   /* Pointer to the GPU memory backing active transform feedback queries,
    * per-stream. Zero if no query is bound.
    */
   uint64_t xfb_query[4];

   struct hk_index_buffer_state index;
   enum agx_primitive topology;
   enum agx_object_type object_type;

   /* Provoking vertex 0, 1, or 2. Usually 0 or 2 for FIRST/LAST. 1 can only be
    * set for tri fans.
    */
   uint8_t provoking;

   struct {
      enum agx_visibility_mode mode;

      /* If enabled, index of the current occlusion query in the occlusion heap.
       * There can only be one active at a time (hardware contraint).
       */
      uint16_t index;
   } occlusion;

   /* Fast linked shader data structures */
   uint64_t varyings;
   struct agx_varyings_vs linked_varyings;

   uint32_t linked_dirty;
   struct hk_linked_shader *linked[PIPE_SHADER_TYPES];
   bool generate_primitive_id;

   /* Tessellation state */
   uint64_t tess_out_draws;

   /* Needed by vk_command_buffer::dynamic_graphics_state */
   struct vk_vertex_input_state _dynamic_vi;
   struct vk_sample_locations_state _dynamic_sl;
};

struct hk_compute_state {
   struct hk_descriptor_state descriptors;
   struct hk_api_shader *shader;
};

struct hk_cmd_push {
   void *map;
   uint64_t addr;
   uint32_t range;
   bool no_prefetch;
};

struct hk_scratch_req {
   bool main;
   bool preamble;
};

/*
 * hk_cs represents a single control stream, to be enqueued either to the
 * CDM or VDM for compute/3D respectively.
 */
enum hk_cs_type {
   HK_CS_CDM,
   HK_CS_VDM,
};

struct hk_cs {
   struct list_head node;

   /* Data master */
   enum hk_cs_type type;

   /* Address of the root control stream for the job */
   uint64_t addr;

   /* Start pointer of the root control stream */
   void *start;

   /* Current pointer within the control stream */
   void *current;

   /* End pointer of the current chunk of the control stream */
   void *end;

   /* Whether there is more than just the root chunk */
   bool stream_linked;

   /* Scratch requirements */
   struct {
      union {
         struct hk_scratch_req vs;
         struct hk_scratch_req cs;
      };

      struct hk_scratch_req fs;
   } scratch;

   /* Immediate writes, type libagx_imm_write. These all happen in parallel at
    * the end of the control stream. This accelerates queries. Implies CDM.
    */
   struct util_dynarray imm_writes;

   /* Statistics */
   struct {
      uint32_t calls, cmds, flushes;
   } stats;

   /* Remaining state is for graphics only, ignored for compute */
   struct agx_tilebuffer_layout tib;

   struct util_dynarray scissor, depth_bias;
   uint64_t uploaded_scissor, uploaded_zbias;

   /* We can only set ppp_multisamplectl once per batch. has_sample_locations
    * tracks if we've committed to a set of sample locations yet. vk_meta
    * operations do not set has_sample_locations since they don't care and it
    * would interfere with the app-provided samples.
    *
    */
   bool has_sample_locations;
   uint32_t ppp_multisamplectl;

   struct hk_render_registers cr;
};

struct hk_uploader {
   /** List of hk_cmd_bo */
   struct list_head bos;

   /* Current addresses */
   uint8_t *map;
   uint64_t base;
   uint32_t offset;
};

struct hk_cmd_buffer {
   struct vk_command_buffer vk;

   struct {
      struct hk_graphics_state gfx;
      struct hk_compute_state cs;
   } state;

   struct {
      struct hk_uploader main, usc;
   } uploader;

   /* List of all recorded control streams */
   struct list_head control_streams;

   /* Current recorded control stream */
   struct {
      /* VDM stream for 3D */
      struct hk_cs *gfx;

      /* CDM stream for compute */
      struct hk_cs *cs;

      /* CDM stream that executes immediately before the current graphics
       * control stream. Used for geometry shading, tessellation, etc.
       */
      struct hk_cs *pre_gfx;

      /* CDM stream that will execute after the current graphics control stream
       * finishes. Used for queries.
       */
      struct hk_cs *post_gfx;
   } current_cs;

   /* Are we currently inside a vk_meta operation? This alters sample location
    * behaviour.
    */
   bool in_meta;

   /* XXX: move me?
    *
    * Indirect draw generated by the pre-GS for the geometry shader.
    */
   uint64_t geom_indirect;

   /* Does the command buffer use the geometry heap? */
   bool uses_heap;

   /* Owned large BOs */
   struct util_dynarray large_bos;
};

VK_DEFINE_HANDLE_CASTS(hk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

extern const struct vk_command_buffer_ops hk_cmd_buffer_ops;

static inline struct hk_device *
hk_cmd_buffer_device(struct hk_cmd_buffer *cmd)
{
   return (struct hk_device *)cmd->vk.base.device;
}

static inline struct hk_cmd_pool *
hk_cmd_buffer_pool(struct hk_cmd_buffer *cmd)
{
   return (struct hk_cmd_pool *)cmd->vk.pool;
}

/*
 * The hardware vertex shader is supplied by the last geometry stage. The
 * geometry pipeline is vertex->tess->geometry so we search backwards.
 */
static inline struct hk_shader *
hk_bound_hw_vs(struct hk_graphics_state *gfx)
{
   struct hk_api_shader *vs = gfx->shaders[MESA_SHADER_VERTEX];
   struct hk_api_shader *tes = gfx->shaders[MESA_SHADER_TESS_EVAL];
   struct hk_api_shader *gs = gfx->shaders[MESA_SHADER_GEOMETRY];

   if (gs)
      return &gs->variants[HK_GS_VARIANT_RAST];
   else if (tes)
      return &tes->variants[HK_VS_VARIANT_HW];
   else
      return &vs->variants[HK_VS_VARIANT_HW];
}

static inline struct hk_shader *
hk_bound_sw_vs(struct hk_graphics_state *gfx)
{
   struct hk_api_shader *vs = gfx->shaders[MESA_SHADER_VERTEX];
   struct hk_shader *hw_vs = hk_bound_hw_vs(gfx);

   if (hw_vs == &vs->variants[HK_VS_VARIANT_HW])
      return hw_vs;
   else
      return &vs->variants[HK_VS_VARIANT_SW];
}

static inline struct hk_shader *
hk_bound_sw_vs_before_gs(struct hk_graphics_state *gfx)
{
   struct hk_api_shader *vs = gfx->shaders[MESA_SHADER_VERTEX];
   struct hk_api_shader *tes = gfx->shaders[MESA_SHADER_TESS_EVAL];
   struct hk_api_shader *api = tes ?: vs;

   return &api->variants[HK_VS_VARIANT_SW];
}

struct agx_ptr hk_pool_alloc_internal(struct hk_cmd_buffer *cmd, uint32_t size,
                                      uint32_t alignment, bool usc);

uint64_t hk_pool_upload(struct hk_cmd_buffer *cmd, const void *data,
                        uint32_t size, uint32_t alignment);

static inline struct agx_ptr
hk_pool_alloc(struct hk_cmd_buffer *cmd, uint32_t size, uint32_t alignment)
{
   return hk_pool_alloc_internal(cmd, size, alignment, false);
}

static inline struct agx_ptr
hk_pool_usc_alloc(struct hk_cmd_buffer *cmd, uint32_t size, uint32_t alignment)
{
   return hk_pool_alloc_internal(cmd, size, alignment, true);
}

void hk_cs_init_graphics(struct hk_cmd_buffer *cmd, struct hk_cs *cs);
uint32_t hk_default_sample_positions(unsigned nr_samples);

static inline struct hk_cs *
hk_cmd_buffer_get_cs_general(struct hk_cmd_buffer *cmd, struct hk_cs **ptr,
                             bool compute)
{
   if ((*ptr) == NULL) {
      /* Allocate root control stream */
      size_t initial_size = 65536;
      struct agx_ptr root = hk_pool_alloc(cmd, initial_size, 1024);
      if (!root.cpu)
         return NULL;

      /* Allocate hk_cs for the new stream */
      struct hk_cs *cs = malloc(sizeof(*cs));
      *cs = (struct hk_cs){
         .type = compute ? HK_CS_CDM : HK_CS_VDM,
         .addr = root.gpu,
         .start = root.cpu,
         .current = root.cpu,
         .end = root.cpu + initial_size,
      };

      list_inithead(&cs->node);

      bool before_gfx = (ptr == &cmd->current_cs.pre_gfx);

      /* Insert into the command buffer. We usually append to the end of the
       * command buffer, except for pre-graphics streams which go right before
       * the graphics workload. (This implies a level of out-of-order processing
       * that's allowed by Vulkan and required for efficient
       * geometry/tessellation shaders.)
       */
      if (before_gfx && cmd->current_cs.gfx) {
         list_addtail(&cs->node, &cmd->current_cs.gfx->node);
      } else {
         list_addtail(&cs->node, &cmd->control_streams);
      }

      *ptr = cs;

      if (!compute)
         hk_cs_init_graphics(cmd, cs);
   }

   assert(*ptr != NULL);
   return *ptr;
}

static inline struct hk_cs *
hk_cmd_buffer_get_cs(struct hk_cmd_buffer *cmd, bool compute)
{
   struct hk_cs **ptr = compute ? &cmd->current_cs.cs : &cmd->current_cs.gfx;
   return hk_cmd_buffer_get_cs_general(cmd, ptr, compute);
}

void hk_ensure_cs_has_space(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                            size_t space);

static void
hk_cmd_buffer_dirty_all(struct hk_cmd_buffer *cmd)
{
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;
   struct hk_graphics_state *gfx = &cmd->state.gfx;

   vk_dynamic_graphics_state_dirty_all(dyn);
   gfx->dirty = ~0;
   gfx->shaders_dirty = ~0;
   gfx->linked_dirty = ~0;
   gfx->descriptors.root_dirty = true;
}

static inline void
hk_cs_destroy(struct hk_cs *cs)
{
   if (cs->type == HK_CS_VDM) {
      util_dynarray_fini(&cs->scissor);
      util_dynarray_fini(&cs->depth_bias);
   } else {
      util_dynarray_fini(&cs->imm_writes);
   }

   free(cs);
}

void hk_dispatch_imm_writes(struct hk_cmd_buffer *cmd, struct hk_cs *cs);

static void
hk_cmd_buffer_end_compute_internal(struct hk_cmd_buffer *cmd,
                                   struct hk_cs **ptr)
{
   if (*ptr) {
      struct hk_cs *cs = *ptr;

      /* This control stream may write immediates as it ends. Queue the writes
       * now that we're done emitting everything else.
       */
      if (cs->imm_writes.size) {
         hk_dispatch_imm_writes(cmd, cs);
      }

      void *map = cs->current;
      agx_push(map, CDM_STREAM_TERMINATE, _)
         ;

      cs->current = map;
   }

   *ptr = NULL;
}

static void
hk_cmd_buffer_end_compute(struct hk_cmd_buffer *cmd)
{
   hk_cmd_buffer_end_compute_internal(cmd, &cmd->current_cs.cs);
}

static void
hk_cmd_buffer_end_graphics(struct hk_cmd_buffer *cmd)
{
   struct hk_cs *cs = cmd->current_cs.gfx;

   if (cs) {
      void *map = cs->current;
      agx_push(map, VDM_STREAM_TERMINATE, _)
         ;

      /* Scissor and depth bias arrays are staged to dynamic arrays on the CPU.
       * When we end the control stream, they're done growing and are ready for
       * upload.
       */
      cs->uploaded_scissor =
         hk_pool_upload(cmd, cs->scissor.data, cs->scissor.size, 64);

      cs->uploaded_zbias =
         hk_pool_upload(cmd, cs->depth_bias.data, cs->depth_bias.size, 64);

      /* TODO: maybe free scissor/depth_bias now? */

      cmd->current_cs.gfx->current = map;
      cmd->current_cs.gfx = NULL;
   }

   hk_cmd_buffer_end_compute_internal(cmd, &cmd->current_cs.pre_gfx);
   hk_cmd_buffer_end_compute_internal(cmd, &cmd->current_cs.post_gfx);

   assert(cmd->current_cs.gfx == NULL);

   /* We just flushed out the heap use. If we want to use it again, we'll need
    * to queue a free for it again.
    */
   cmd->uses_heap = false;
}

static inline uint64_t
hk_pipeline_stat_addr(struct hk_cmd_buffer *cmd,
                      VkQueryPipelineStatisticFlagBits stat)
{
   struct hk_root_descriptor_table *root = &cmd->state.gfx.descriptors.root;
   VkQueryPipelineStatisticFlags flags = root->draw.pipeline_stats_flags;

   if (flags & stat) {
      assert(!cmd->in_meta && "queries paused for meta");
      assert(util_bitcount(stat) == 1 && "by construction");

      /* Prefix sum to determine the compacted index in the query pool */
      uint32_t index = util_bitcount(flags & (stat - 1));

      return root->draw.pipeline_stats + (sizeof(uint64_t) * index);
   } else {
      /* Query disabled */
      return 0;
   }
}

void hk_cmd_buffer_begin_graphics(struct hk_cmd_buffer *cmd,
                                  const VkCommandBufferBeginInfo *pBeginInfo);
void hk_cmd_buffer_begin_compute(struct hk_cmd_buffer *cmd,
                                 const VkCommandBufferBeginInfo *pBeginInfo);

void hk_cmd_invalidate_graphics_state(struct hk_cmd_buffer *cmd);
void hk_cmd_invalidate_compute_state(struct hk_cmd_buffer *cmd);

void hk_cmd_bind_shaders(struct vk_command_buffer *vk_cmd, uint32_t stage_count,
                         const gl_shader_stage *stages,
                         struct vk_shader **const shaders);

void hk_cmd_bind_graphics_shader(struct hk_cmd_buffer *cmd,
                                 const gl_shader_stage stage,
                                 struct hk_api_shader *shader);

void hk_cmd_bind_compute_shader(struct hk_cmd_buffer *cmd,
                                struct hk_api_shader *shader);

void hk_cmd_bind_vertex_buffer(struct hk_cmd_buffer *cmd, uint32_t vb_idx,
                               struct hk_addr_range addr_range);

static inline struct hk_descriptor_state *
hk_get_descriptors_state(struct hk_cmd_buffer *cmd,
                         VkPipelineBindPoint bind_point)
{
   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmd->state.gfx.descriptors;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmd->state.cs.descriptors;
   default:
      unreachable("Unhandled bind point");
   }
};

void hk_cmd_buffer_flush_push_descriptors(struct hk_cmd_buffer *cmd,
                                          struct hk_descriptor_state *desc);

void hk_meta_resolve_rendering(struct hk_cmd_buffer *cmd,
                               const VkRenderingInfo *pRenderingInfo);

uint64_t hk_cmd_buffer_upload_root(struct hk_cmd_buffer *cmd,
                                   VkPipelineBindPoint bind_point);

void hk_reserve_scratch(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                        struct hk_shader *s);

uint32_t hk_upload_usc_words(struct hk_cmd_buffer *cmd, struct hk_shader *s,
                             struct hk_linked_shader *linked);

uint32_t hk_upload_usc_words_kernel(struct hk_cmd_buffer *cmd,
                                    struct hk_shader *s, void *data,
                                    size_t data_size);

void hk_usc_upload_spilled_rt_descs(struct agx_usc_builder *b,
                                    struct hk_cmd_buffer *cmd);

void hk_cdm_cache_flush(struct hk_device *dev, struct hk_cs *cs);

struct hk_grid {
   bool indirect;
   union {
      uint32_t count[3];
      uint64_t ptr;
   };
};

static struct hk_grid
hk_grid(uint32_t x, uint32_t y, uint32_t z)
{
   return (struct hk_grid){.indirect = false, .count = {x, y, z}};
}

static struct hk_grid
hk_grid_indirect(uint64_t ptr)
{
   return (struct hk_grid){.indirect = true, .ptr = ptr};
}

void hk_dispatch_with_usc(struct hk_device *dev, struct hk_cs *cs,
                          struct hk_shader *s, uint32_t usc,
                          struct hk_grid grid, struct hk_grid local_size);

static inline void
hk_dispatch_with_local_size(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                            struct hk_shader *s, struct hk_grid grid,
                            struct hk_grid local_size)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   uint32_t usc = hk_upload_usc_words(cmd, s, s->only_linked);

   hk_reserve_scratch(cmd, cs, s);
   hk_dispatch_with_usc(dev, cs, s, usc, grid, local_size);
}

static inline void
hk_dispatch(struct hk_cmd_buffer *cmd, struct hk_cs *cs, struct hk_shader *s,
            struct hk_grid grid)
{
   assert(s->info.stage == MESA_SHADER_COMPUTE);

   struct hk_grid local_size =
      hk_grid(s->info.cs.local_size[0], s->info.cs.local_size[1],
              s->info.cs.local_size[2]);

   if (!grid.indirect) {
      grid.count[0] *= local_size.count[0];
      grid.count[1] *= local_size.count[1];
      grid.count[2] *= local_size.count[2];
   }

   hk_dispatch_with_local_size(cmd, cs, s, grid, local_size);
}

void hk_queue_write(struct hk_cmd_buffer *cmd, uint64_t address, uint32_t value,
                    bool after_gfx);
