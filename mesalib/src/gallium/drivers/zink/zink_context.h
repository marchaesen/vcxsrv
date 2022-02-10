/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ZINK_CONTEXT_H
#define ZINK_CONTEXT_H

#define ZINK_FBFETCH_BINDING 6 //COMPUTE+1
#define ZINK_SHADER_COUNT (PIPE_SHADER_TYPES - 1)

#define ZINK_DEFAULT_MAX_DESCS 5000
#define ZINK_DEFAULT_DESC_CLAMP (ZINK_DEFAULT_MAX_DESCS * 0.9)

#define ZINK_MAX_BINDLESS_HANDLES 1024

#include "zink_clear.h"
#include "zink_pipeline.h"
#include "zink_batch.h"
#include "zink_compiler.h"
#include "zink_descriptors.h"
#include "zink_surface.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_rect.h"
#include "util/u_threaded_context.h"
#include "util/u_idalloc.h"
#include "util/slab.h"
#include "util/list.h"
#include "util/u_dynarray.h"

#include <vulkan/vulkan.h>

#define pipe_buffer_write "use tc_buffer_write to avoid breaking threaded context"

#ifdef __cplusplus
extern "C" {
#endif

struct blitter_context;
struct list_head;

struct zink_blend_state;
struct zink_depth_stencil_alpha_state;
struct zink_gfx_program;
struct zink_rasterizer_state;
struct zink_resource;
struct zink_vertex_elements_state;

enum zink_blit_flags {
   ZINK_BLIT_NORMAL = 1 << 0,
   ZINK_BLIT_SAVE_FS = 1 << 1,
   ZINK_BLIT_SAVE_FB = 1 << 2,
   ZINK_BLIT_SAVE_TEXTURES = 1 << 3,
   ZINK_BLIT_NO_COND_RENDER = 1 << 4,
};

struct zink_sampler_state {
   VkSampler sampler;
   uint32_t hash;
   struct zink_descriptor_refs desc_set_refs;
   struct zink_batch_usage *batch_uses;
   bool custom_border_color;
};

struct zink_buffer_view {
   struct pipe_reference reference;
   struct pipe_resource *pres;
   VkBufferViewCreateInfo bvci;
   VkBufferView buffer_view;
   uint32_t hash;
   struct zink_batch_usage *batch_uses;
   struct zink_descriptor_refs desc_set_refs;
};

struct zink_sampler_view {
   struct pipe_sampler_view base;
   union {
      struct zink_surface *image_view;
      struct zink_buffer_view *buffer_view;
   };
};

struct zink_image_view {
   struct pipe_image_view base;
   union {
      struct zink_surface *surface;
      struct zink_buffer_view *buffer_view;
   };
};

static inline struct zink_sampler_view *
zink_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct zink_sampler_view *)pview;
}

struct zink_so_target {
   struct pipe_stream_output_target base;
   struct pipe_resource *counter_buffer;
   VkDeviceSize counter_buffer_offset;
   uint32_t stride;
   bool counter_buffer_valid;
};

static inline struct zink_so_target *
zink_so_target(struct pipe_stream_output_target *so_target)
{
   return (struct zink_so_target *)so_target;
}

struct zink_viewport_state {
   struct pipe_viewport_state viewport_states[PIPE_MAX_VIEWPORTS];
   struct pipe_scissor_state scissor_states[PIPE_MAX_VIEWPORTS];
   uint8_t num_viewports;
};


struct zink_descriptor_surface {
   union {
      struct zink_surface *surface;
      struct zink_buffer_view *bufferview;
   };
   bool is_buffer;
};

struct zink_bindless_descriptor {
   struct zink_descriptor_surface ds;
   struct zink_sampler_state *sampler;
   uint32_t handle;
   uint32_t access; //PIPE_ACCESS_...
};

static inline struct zink_resource *
zink_descriptor_surface_resource(struct zink_descriptor_surface *ds)
{
   return ds->is_buffer ? (struct zink_resource*)ds->bufferview->pres : (struct zink_resource*)ds->surface->base.texture;
}

typedef void (*pipe_draw_vbo_func)(struct pipe_context *pipe,
                                   const struct pipe_draw_info *info,
                                   unsigned drawid_offset,
                                   const struct pipe_draw_indirect_info *indirect,
                                   const struct pipe_draw_start_count_bias *draws,
                                   unsigned num_draws);
typedef void (*pipe_draw_vertex_state_func)(struct pipe_context *ctx,
                                            struct pipe_vertex_state *vstate,
                                            uint32_t partial_velem_mask,
                                            struct pipe_draw_vertex_state_info info,
                                            const struct pipe_draw_start_count_bias *draws,
                                            unsigned num_draws);
typedef void (*pipe_launch_grid_func)(struct pipe_context *pipe, const struct pipe_grid_info *info);

typedef enum {
   ZINK_NO_MULTIDRAW,
   ZINK_MULTIDRAW,
} zink_multidraw;

typedef enum {
   ZINK_NO_DYNAMIC_STATE,
   ZINK_DYNAMIC_STATE,
   ZINK_DYNAMIC_STATE2,
   ZINK_DYNAMIC_VERTEX_INPUT,
} zink_dynamic_state;

struct zink_context {
   struct pipe_context base;
   struct threaded_context *tc;
   struct slab_child_pool transfer_pool;
   struct slab_child_pool transfer_pool_unsync;
   struct blitter_context *blitter;

   pipe_draw_vbo_func draw_vbo[2]; //batch changed
   pipe_draw_vertex_state_func draw_state[2]; //batch changed
   pipe_launch_grid_func launch_grid[2]; //batch changed

   struct pipe_device_reset_callback reset;

   simple_mtx_t batch_mtx;
   struct zink_fence *deferred_fence;
   struct zink_fence *last_fence; //the last command buffer submitted
   struct zink_batch_state *batch_states; //list of submitted batch states: ordered by increasing timeline id
   unsigned batch_states_count; //number of states in `batch_states`
   struct util_dynarray free_batch_states; //unused batch states
   bool oom_flush;
   bool oom_stall;
   struct zink_batch batch;

   unsigned shader_has_inlinable_uniforms_mask;
   unsigned inlinable_uniforms_valid_mask;

   struct pipe_constant_buffer ubos[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_shader_buffer ssbos[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];
   uint32_t writable_ssbos[PIPE_SHADER_TYPES];
   struct zink_image_view image_views[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];

   struct pipe_framebuffer_state fb_state;
   struct zink_framebuffer *(*get_framebuffer)(struct zink_context*);
   void (*init_framebuffer)(struct zink_screen *screen, struct zink_framebuffer *fb, struct zink_render_pass *rp);
   struct hash_table framebuffer_cache;

   struct zink_vertex_elements_state *element_state;
   struct zink_rasterizer_state *rast_state;
   struct zink_depth_stencil_alpha_state *dsa_state;

   struct hash_table desc_set_layouts[ZINK_DESCRIPTOR_TYPES];
   struct set desc_pool_keys[ZINK_DESCRIPTOR_TYPES];
   bool pipeline_changed[2]; //gfx, compute

   struct zink_shader *gfx_stages[ZINK_SHADER_COUNT];
   struct zink_shader *last_vertex_stage;
   bool shader_reads_drawid;
   bool shader_reads_basevertex;
   struct zink_gfx_pipeline_state gfx_pipeline_state;
   /* there are 5 gfx stages, but VS and FS are assumed to be always present,
    * thus only 3 stages need to be considered, giving 2^3 = 8 program caches.
    */
   struct hash_table program_cache[8];
   uint32_t gfx_hash;
   struct zink_gfx_program *curr_program;

   struct zink_descriptor_data *dd;

   struct zink_shader *compute_stage;
   struct zink_compute_pipeline_state compute_pipeline_state;
   struct hash_table compute_program_cache;
   struct zink_compute_program *curr_compute;

   unsigned shader_stages : ZINK_SHADER_COUNT; /* mask of bound gfx shader stages */
   unsigned dirty_shader_stages : 6; /* mask of changed shader stages */
   bool last_vertex_stage_dirty;

   struct set render_pass_state_cache;
   struct hash_table *render_pass_cache;
   bool new_swapchain;
   bool fb_changed;
   bool rp_changed;

   struct zink_framebuffer *framebuffer;
   struct zink_framebuffer_clear fb_clears[PIPE_MAX_COLOR_BUFS + 1];
   uint16_t clears_enabled;
   uint16_t rp_clears_enabled;
   uint16_t fbfetch_outputs;

   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   bool vertex_buffers_dirty;

   void *sampler_states[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   struct pipe_sampler_view *sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];

   struct zink_viewport_state vp_state;
   bool vp_state_changed;
   bool scissor_changed;

   float blend_constants[4];

   bool sample_locations_changed;
   VkSampleLocationEXT vk_sample_locations[PIPE_MAX_SAMPLE_LOCATION_GRID_SIZE * PIPE_MAX_SAMPLE_LOCATION_GRID_SIZE];
   uint8_t sample_locations[2 * 4 * 8 * 16];

   struct pipe_stencil_ref stencil_ref;

   union {
      struct {
         float default_inner_level[2];
         float default_outer_level[4];
      };
      float tess_levels[6];
   };

   struct list_head suspended_queries;
   struct list_head primitives_generated_queries;
   bool queries_disabled, render_condition_active;
   struct {
      struct zink_query *query;
      bool inverted;
   } render_condition;

   struct pipe_resource *dummy_vertex_buffer;
   struct pipe_resource *dummy_xfb_buffer;
   struct pipe_surface *dummy_surface[7];
   struct zink_buffer_view *dummy_bufferview;

   unsigned buffer_rebind_counter;

   struct {
      /* descriptor info */
      VkDescriptorBufferInfo ubos[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
      uint32_t push_valid;
      uint8_t num_ubos[PIPE_SHADER_TYPES];

      VkDescriptorBufferInfo ssbos[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];
      uint8_t num_ssbos[PIPE_SHADER_TYPES];

      VkDescriptorImageInfo textures[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      VkBufferView tbos[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      uint8_t num_samplers[PIPE_SHADER_TYPES];
      uint8_t num_sampler_views[PIPE_SHADER_TYPES];

      VkDescriptorImageInfo images[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
      VkBufferView texel_images[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
      uint8_t num_images[PIPE_SHADER_TYPES];

      VkDescriptorImageInfo fbfetch;

      struct zink_resource *descriptor_res[ZINK_DESCRIPTOR_TYPES][PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      struct zink_descriptor_surface sampler_surfaces[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      struct zink_descriptor_surface image_surfaces[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];

      struct {
         struct util_idalloc tex_slots;
         struct util_idalloc img_slots;
         struct hash_table tex_handles;
         struct hash_table img_handles;
         VkBufferView *buffer_infos; //tex, img
         VkDescriptorImageInfo *img_infos; //tex, img
         struct util_dynarray updates;
         struct util_dynarray resident;
      } bindless[2];  //img, buffer
      union {
         bool bindless_dirty[2]; //tex, img
         uint16_t any_bindless_dirty;
      };
      bool bindless_refs_dirty;
   } di;
   struct set *need_barriers[2]; //gfx, compute
   struct set update_barriers[2][2]; //[gfx, compute][current, next]
   uint8_t barrier_set_idx[2];
   unsigned memory_barrier;

   uint32_t num_so_targets;
   struct pipe_stream_output_target *so_targets[PIPE_MAX_SO_OUTPUTS];
   bool dirty_so_targets;
   bool xfb_barrier;
   bool first_frame_done;
   bool have_timelines;

   bool gfx_dirty;

   bool is_device_lost;
   bool primitive_restart;
   bool vertex_state_changed : 1;
   bool blend_state_changed : 1;
   bool rast_state_changed : 1;
   bool dsa_state_changed : 1;
   bool stencil_ref_changed : 1;
};

static inline struct zink_context *
zink_context(struct pipe_context *context)
{
   return (struct zink_context *)context;
}

static inline bool
zink_fb_clear_enabled(const struct zink_context *ctx, unsigned idx)
{
   if (idx == PIPE_MAX_COLOR_BUFS)
      return ctx->clears_enabled & PIPE_CLEAR_DEPTHSTENCIL;
   return ctx->clears_enabled & (PIPE_CLEAR_COLOR0 << idx);
}

void
zink_fence_wait(struct pipe_context *ctx);

void
zink_wait_on_batch(struct zink_context *ctx, uint32_t batch_id);

bool
zink_check_batch_completion(struct zink_context *ctx, uint32_t batch_id, bool have_lock);

void
zink_flush_queue(struct zink_context *ctx);
void
zink_update_fbfetch(struct zink_context *ctx);
bool
zink_resource_access_is_write(VkAccessFlags flags);

void
zink_resource_buffer_barrier(struct zink_context *ctx, struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline);
bool
zink_resource_image_needs_barrier(struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
bool
zink_resource_image_barrier_init(VkImageMemoryBarrier *imb, struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
void
zink_resource_image_barrier(struct zink_context *ctx, struct zink_resource *res,
                      VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);

bool
zink_resource_needs_barrier(struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
void
zink_update_descriptor_refs(struct zink_context *ctx, bool compute);
void
zink_init_vk_sample_locations(struct zink_context *ctx, VkSampleLocationsInfoEXT *loc);

void
zink_begin_render_pass(struct zink_context *ctx);
void
zink_end_render_pass(struct zink_context *ctx);

static inline void
zink_batch_rp(struct zink_context *ctx)
{
   if (!ctx->batch.in_rp)
      zink_begin_render_pass(ctx);
}

static inline void
zink_batch_no_rp(struct zink_context *ctx)
{
   zink_end_render_pass(ctx);
   assert(!ctx->batch.in_rp);
}

static inline VkPipelineStageFlags
zink_pipeline_flags_from_pipe_stage(enum pipe_shader_type pstage)
{
   switch (pstage) {
   case PIPE_SHADER_VERTEX:
      return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
   case PIPE_SHADER_FRAGMENT:
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
   case PIPE_SHADER_GEOMETRY:
      return VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
   case PIPE_SHADER_TESS_CTRL:
      return VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
   case PIPE_SHADER_TESS_EVAL:
      return VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
   case PIPE_SHADER_COMPUTE:
      return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   default:
      unreachable("unknown shader stage");
   }
}

void
zink_rebind_all_buffers(struct zink_context *ctx);

void
zink_flush_memory_barrier(struct zink_context *ctx, bool is_compute);
void
zink_init_draw_functions(struct zink_context *ctx, struct zink_screen *screen);
void
zink_init_grid_functions(struct zink_context *ctx);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
VkPipelineStageFlags
zink_pipeline_flags_from_stage(VkShaderStageFlagBits stage);

VkShaderStageFlagBits
zink_shader_stage(enum pipe_shader_type type);

struct pipe_context *
zink_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags);

void
zink_context_query_init(struct pipe_context *ctx);

void
zink_blit_begin(struct zink_context *ctx, enum zink_blit_flags flags);

void
zink_blit(struct pipe_context *pctx,
          const struct pipe_blit_info *info);

bool
zink_blit_region_fills(struct u_rect region, unsigned width, unsigned height);

bool
zink_blit_region_covers(struct u_rect region, struct u_rect covers);

static inline struct u_rect
zink_rect_from_box(const struct pipe_box *box)
{
   return (struct u_rect){box->x, box->x + box->width, box->y, box->y + box->height};
}

static inline VkComponentSwizzle
zink_component_mapping(enum pipe_swizzle swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_X: return VK_COMPONENT_SWIZZLE_R;
   case PIPE_SWIZZLE_Y: return VK_COMPONENT_SWIZZLE_G;
   case PIPE_SWIZZLE_Z: return VK_COMPONENT_SWIZZLE_B;
   case PIPE_SWIZZLE_W: return VK_COMPONENT_SWIZZLE_A;
   case PIPE_SWIZZLE_0: return VK_COMPONENT_SWIZZLE_ZERO;
   case PIPE_SWIZZLE_1: return VK_COMPONENT_SWIZZLE_ONE;
   case PIPE_SWIZZLE_NONE: return VK_COMPONENT_SWIZZLE_IDENTITY; // ???
   default:
      unreachable("unexpected swizzle");
   }
}

enum pipe_swizzle
zink_clamp_void_swizzle(const struct util_format_description *desc, enum pipe_swizzle swizzle);

bool
zink_resource_rebind(struct zink_context *ctx, struct zink_resource *res);

void
zink_rebind_framebuffer(struct zink_context *ctx, struct zink_resource *res);

void
zink_copy_buffer(struct zink_context *ctx, struct zink_resource *dst, struct zink_resource *src,
                 unsigned dst_offset, unsigned src_offset, unsigned size);

void
zink_copy_image_buffer(struct zink_context *ctx, struct zink_resource *dst, struct zink_resource *src,
                       unsigned dst_level, unsigned dstx, unsigned dsty, unsigned dstz,
                       unsigned src_level, const struct pipe_box *src_box, enum pipe_map_flags map_flags);

void
zink_destroy_buffer_view(struct zink_screen *screen, struct zink_buffer_view *buffer_view);

void
debug_describe_zink_buffer_view(char *buf, const struct zink_buffer_view *ptr);

static inline void
zink_buffer_view_reference(struct zink_screen *screen,
                           struct zink_buffer_view **dst,
                           struct zink_buffer_view *src)
{
   struct zink_buffer_view *old_dst = dst ? *dst : NULL;

   if (pipe_reference_described(old_dst ? &old_dst->reference : NULL, &src->reference,
                                (debug_reference_descriptor)debug_describe_zink_buffer_view))
      zink_destroy_buffer_view(screen, old_dst);
   if (dst) *dst = src;
}
#endif

#endif
