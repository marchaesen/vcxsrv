/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_PIPELINE_H
#define TU_PIPELINE_H

#include "tu_common.h"

#include "tu_cs.h"
#include "tu_descriptor_set.h"
#include "tu_shader.h"
#include "tu_suballoc.h"

enum tu_dynamic_state
{
   /* re-use VK_DYNAMIC_STATE_ enums for non-extended dynamic states */
   TU_DYNAMIC_STATE_SAMPLE_LOCATIONS = VK_DYNAMIC_STATE_STENCIL_REFERENCE + 1,
   TU_DYNAMIC_STATE_RB_DEPTH_CNTL,
   TU_DYNAMIC_STATE_RB_STENCIL_CNTL,
   TU_DYNAMIC_STATE_VB_STRIDE,
   TU_DYNAMIC_STATE_RASTERIZER_DISCARD,
   TU_DYNAMIC_STATE_BLEND,
   TU_DYNAMIC_STATE_VERTEX_INPUT,
   TU_DYNAMIC_STATE_COUNT,
   /* no associated draw state: */
   TU_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY = TU_DYNAMIC_STATE_COUNT,
   TU_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
   TU_DYNAMIC_STATE_LOGIC_OP,
   TU_DYNAMIC_STATE_COLOR_WRITE_ENABLE,
   /* re-use the line width enum as it uses GRAS_SU_CNTL: */
   TU_DYNAMIC_STATE_GRAS_SU_CNTL = VK_DYNAMIC_STATE_LINE_WIDTH,
};

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
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_cache, base, VkPipelineCache,
                               VK_OBJECT_TYPE_PIPELINE_CACHE)

struct tu_lrz_pipeline
{
   uint32_t force_disable_mask;
   bool fs_has_kill;
   bool force_late_z;
   bool early_fragment_tests;
};

struct tu_compiled_shaders
{
   struct vk_pipeline_cache_object base;

   struct tu_push_constant_range push_consts[MESA_SHADER_STAGES];
   uint8_t active_desc_sets;
   bool multi_pos_output;

   struct ir3_shader_variant *variants[MESA_SHADER_STAGES];
};

extern const struct vk_pipeline_cache_object_ops tu_shaders_ops;

static bool inline
tu6_shared_constants_enable(const struct tu_pipeline_layout *layout,
                            const struct ir3_compiler *compiler)
{
   return layout->push_constant_size > 0 &&
          layout->push_constant_size <= (compiler->shared_consts_size * 16);
}

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
   struct tu_suballoc_bo bo;

   /* Separate BO for private memory since it should GPU writable */
   struct tu_bo *pvtmem_bo;

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

   unsigned num_rts;
   uint32_t rb_mrt_control[MAX_RTS], rb_mrt_control_mask;
   uint32_t rb_mrt_blend_control[MAX_RTS];
   uint32_t sp_blend_cntl, sp_blend_cntl_mask;
   uint32_t rb_blend_cntl, rb_blend_cntl_mask;
   uint32_t color_write_enable, blend_enable;
   bool logic_op_enabled, rop_reads_dst;
   bool rasterizer_discard;

   bool rb_depth_cntl_disable;

   enum a5xx_line_mode line_mode;

   /* draw states for the pipeline */
   struct tu_draw_state load_state, rast_state;
   struct tu_draw_state prim_order_state_sysmem, prim_order_state_gmem;

   /* for vertex buffers state */
   uint32_t num_vbs;

   struct tu_push_constant_range shared_consts;

   struct
   {
      struct tu_draw_state config_state;
      struct tu_draw_state state;
      struct tu_draw_state binning_state;

      struct tu_program_descriptor_linkage link[MESA_SHADER_STAGES];
   } program;

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

   /* In other words - framebuffer fetch support */
   bool raster_order_attachment_access;
   bool subpass_feedback_loop_ds;
   bool feedback_loop_may_involve_textures;
   /* If the pipeline sets SINGLE_PRIM_MODE for sysmem. */
   bool sysmem_single_prim_mode;

   bool z_negative_one_to_one;

   /* memory bandwidth cost (in bytes) for color attachments */
   uint32_t color_bandwidth_per_sample;

   uint32_t depth_cpp_per_sample;
   uint32_t stencil_cpp_per_sample;

   void *executables_mem_ctx;
   /* tu_pipeline_executable */
   struct util_dynarray executables;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)

void
tu6_emit_viewport(struct tu_cs *cs, const VkViewport *viewport, uint32_t num_viewport,
                  bool z_negative_one_to_one);

void
tu6_emit_scissor(struct tu_cs *cs, const VkRect2D *scs, uint32_t scissor_count);

void
tu6_emit_sample_locations(struct tu_cs *cs, const VkSampleLocationsInfoEXT *samp_loc);

void
tu6_emit_depth_bias(struct tu_cs *cs,
                    float constant_factor,
                    float clamp,
                    float slope_factor);

#define TU6_EMIT_VERTEX_INPUT_MAX_DWORDS (MAX_VERTEX_ATTRIBS * 2 + 1)

void tu6_emit_vertex_input(struct tu_cs *cs,
                           uint32_t binding_count,
                           const VkVertexInputBindingDescription2EXT *bindings,
                           uint32_t attr_count,
                           const VkVertexInputAttributeDescription2EXT *attrs);

uint32_t tu6_rb_mrt_control_rop(VkLogicOp op, bool *rop_reads_dst);

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

#endif /* TU_PIPELINE_H */
