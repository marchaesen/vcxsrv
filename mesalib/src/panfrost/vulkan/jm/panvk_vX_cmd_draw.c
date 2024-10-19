/*
 * Copyright © 2024 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_meta.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_instance.h"
#include "panvk_priv_bo.h"
#include "panvk_shader.h"

#include "pan_desc.h"
#include "pan_earlyzs.h"
#include "pan_encoder.h"
#include "pan_format.h"
#include "pan_jc.h"
#include "pan_props.h"
#include "pan_shader.h"

#include "vk_format.h"
#include "vk_meta.h"
#include "vk_pipeline_layout.h"

struct panvk_draw_info {
   unsigned first_index;
   unsigned index_count;
   unsigned index_size;
   unsigned first_vertex;
   unsigned vertex_count;
   unsigned vertex_range;
   unsigned padded_vertex_count;
   unsigned first_instance;
   unsigned instance_count;
   int vertex_offset;
   unsigned offset_start;
   uint32_t layer_id;
   struct mali_invocation_packed invocation;
   struct {
      mali_ptr varyings;
      mali_ptr attributes;
      mali_ptr attribute_bufs;
   } vs;
   struct {
      mali_ptr rsd;
      mali_ptr varyings;
   } fs;
   mali_ptr push_uniforms;
   mali_ptr varying_bufs;
   mali_ptr position;
   mali_ptr indices;
   union {
      mali_ptr psiz;
      float line_width;
   };
   mali_ptr tls;
   mali_ptr fb;
   const struct pan_tiler_context *tiler_ctx;
   mali_ptr viewport;
   struct {
      struct panfrost_ptr vertex_copy_desc;
      struct panfrost_ptr frag_copy_desc;
      union {
         struct {
            struct panfrost_ptr vertex;
            struct panfrost_ptr tiler;
         };
         struct panfrost_ptr idvs;
      };
   } jobs;
};

#define is_dirty(__cmdbuf, __name)                                             \
   BITSET_TEST((__cmdbuf)->vk.dynamic_graphics_state.dirty,                    \
               MESA_VK_DYNAMIC_##__name)

static VkResult
panvk_cmd_prepare_draw_sysvals(struct panvk_cmd_buffer *cmdbuf,
                               struct panvk_draw_info *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *fs = cmdbuf->state.gfx.fs.shader;

   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_graphics_sysvals *sysvals = &cmdbuf->state.gfx.sysvals;
   struct vk_color_blend_state *cb = &cmdbuf->vk.dynamic_graphics_state.cb;
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   unsigned base_vertex = draw->index_size ? draw->vertex_offset : 0;
   if (sysvals->vs.first_vertex != draw->offset_start ||
       sysvals->vs.base_vertex != base_vertex ||
       sysvals->vs.base_instance != draw->first_instance ||
       sysvals->layer_id != draw->layer_id ||
       sysvals->fs.multisampled != (fbinfo->nr_samples > 1)) {
      sysvals->vs.first_vertex = draw->offset_start;
      sysvals->vs.base_vertex = base_vertex;
      sysvals->vs.base_instance = draw->first_instance;
      sysvals->layer_id = draw->layer_id;
      sysvals->fs.multisampled = fbinfo->nr_samples > 1;
      cmdbuf->state.gfx.push_uniforms = 0;
   }

   if (is_dirty(cmdbuf, CB_BLEND_CONSTANTS)) {
      for (unsigned i = 0; i < ARRAY_SIZE(cb->blend_constants); i++)
         sysvals->blend.constants[i] =
            CLAMP(cb->blend_constants[i], 0.0f, 1.0f);
      cmdbuf->state.gfx.push_uniforms = 0;
   }

   if (is_dirty(cmdbuf, VP_VIEWPORTS)) {
      VkViewport *viewport = &cmdbuf->vk.dynamic_graphics_state.vp.viewports[0];

      /* Upload the viewport scale. Defined as (px/2, py/2, pz) at the start of
       * section 24.5 ("Controlling the Viewport") of the Vulkan spec. At the
       * end of the section, the spec defines:
       *
       * px = width
       * py = height
       * pz = maxDepth - minDepth
       */
      sysvals->viewport.scale.x = 0.5f * viewport->width;
      sysvals->viewport.scale.y = 0.5f * viewport->height;
      sysvals->viewport.scale.z = (viewport->maxDepth - viewport->minDepth);

      /* Upload the viewport offset. Defined as (ox, oy, oz) at the start of
       * section 24.5 ("Controlling the Viewport") of the Vulkan spec. At the
       * end of the section, the spec defines:
       *
       * ox = x + width/2
       * oy = y + height/2
       * oz = minDepth
       */
      sysvals->viewport.offset.x = (0.5f * viewport->width) + viewport->x;
      sysvals->viewport.offset.y = (0.5f * viewport->height) + viewport->y;
      sysvals->viewport.offset.z = viewport->minDepth;
      cmdbuf->state.gfx.push_uniforms = 0;
   }

   VkResult result = panvk_per_arch(cmd_prepare_dyn_ssbos)(cmdbuf, desc_state,
                                                           vs, vs_desc_state);
   if (result != VK_SUCCESS)
      return result;

   sysvals->desc.vs_dyn_ssbos = vs_desc_state->dyn_ssbos;
   result = panvk_per_arch(cmd_prepare_dyn_ssbos)(cmdbuf, desc_state, fs,
                                                  fs_desc_state);
   if (result != VK_SUCCESS)
      return result;

   sysvals->desc.fs_dyn_ssbos = fs_desc_state->dyn_ssbos;

   for (uint32_t i = 0; i < MAX_SETS; i++) {
      uint32_t used_set_mask =
         vs->desc_info.used_set_mask | (fs ? fs->desc_info.used_set_mask : 0);

      if (used_set_mask & BITFIELD_BIT(i))
         sysvals->desc.sets[i] = desc_state->sets[i]->descs.dev;
   }

   return VK_SUCCESS;
}

static bool
has_depth_att(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->state.gfx.render.bound_attachments &
           MESA_VK_RP_ATTACHMENT_DEPTH_BIT) != 0;
}

static bool
has_stencil_att(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->state.gfx.render.bound_attachments &
           MESA_VK_RP_ATTACHMENT_STENCIL_BIT) != 0;
}

static bool
writes_depth(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   return has_depth_att(cmdbuf) && ds->depth.test_enable &&
          ds->depth.write_enable && ds->depth.compare_op != VK_COMPARE_OP_NEVER;
}

static bool
writes_stencil(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   return has_stencil_att(cmdbuf) && ds->stencil.test_enable &&
          ((ds->stencil.front.write_mask &&
            (ds->stencil.front.op.fail != VK_STENCIL_OP_KEEP ||
             ds->stencil.front.op.pass != VK_STENCIL_OP_KEEP ||
             ds->stencil.front.op.depth_fail != VK_STENCIL_OP_KEEP)) ||
           (ds->stencil.back.write_mask &&
            (ds->stencil.back.op.fail != VK_STENCIL_OP_KEEP ||
             ds->stencil.back.op.pass != VK_STENCIL_OP_KEEP ||
             ds->stencil.back.op.depth_fail != VK_STENCIL_OP_KEEP)));
}

static bool
ds_test_always_passes(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   if (!has_depth_att(cmdbuf))
      return true;

   if (ds->depth.test_enable && ds->depth.compare_op != VK_COMPARE_OP_ALWAYS)
      return false;

   if (ds->stencil.test_enable &&
       (ds->stencil.front.op.compare != VK_COMPARE_OP_ALWAYS ||
        ds->stencil.back.op.compare != VK_COMPARE_OP_ALWAYS))
      return false;

   return true;
}

static inline enum mali_func
translate_compare_func(VkCompareOp comp)
{
   STATIC_ASSERT(VK_COMPARE_OP_NEVER == (VkCompareOp)MALI_FUNC_NEVER);
   STATIC_ASSERT(VK_COMPARE_OP_LESS == (VkCompareOp)MALI_FUNC_LESS);
   STATIC_ASSERT(VK_COMPARE_OP_EQUAL == (VkCompareOp)MALI_FUNC_EQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_LESS_OR_EQUAL == (VkCompareOp)MALI_FUNC_LEQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_GREATER == (VkCompareOp)MALI_FUNC_GREATER);
   STATIC_ASSERT(VK_COMPARE_OP_NOT_EQUAL == (VkCompareOp)MALI_FUNC_NOT_EQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_GREATER_OR_EQUAL ==
                 (VkCompareOp)MALI_FUNC_GEQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_ALWAYS == (VkCompareOp)MALI_FUNC_ALWAYS);

   return (enum mali_func)comp;
}

static enum mali_stencil_op
translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP:
      return MALI_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO:
      return MALI_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE:
      return MALI_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
      return MALI_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
      return MALI_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:
      return MALI_STENCIL_OP_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:
      return MALI_STENCIL_OP_DECR_WRAP;
   case VK_STENCIL_OP_INVERT:
      return MALI_STENCIL_OP_INVERT;
   default:
      unreachable("Invalid stencil op");
   }
}

static bool
fs_required(struct panvk_cmd_buffer *cmdbuf)
{
   const struct pan_shader_info *fs_info =
      cmdbuf->state.gfx.fs.shader ? &cmdbuf->state.gfx.fs.shader->info : NULL;
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_color_blend_state *cb = &dyns->cb;

   if (!fs_info)
      return false;

   /* If we generally have side effects */
   if (fs_info->fs.sidefx)
      return true;

   /* If colour is written we need to execute */
   for (unsigned i = 0; i < cb->attachment_count; ++i) {
      if ((cb->color_write_enables & BITFIELD_BIT(i)) &&
          cb->attachments[i].write_mask)
         return true;
   }

   /* If alpha-to-coverage is enabled, we need to run the fragment shader even
    * if we don't have a color attachment, so depth/stencil updates can be
    * discarded if alpha, and thus coverage, is 0. */
   if (dyns->ms.alpha_to_coverage_enable)
      return true;

   /* If depth is written and not implied we need to execute.
    * TODO: Predicate on Z/S writes being enabled */
   return (fs_info->fs.writes_depth || fs_info->fs.writes_stencil);
}

static VkResult
panvk_draw_prepare_fs_rsd(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_draw_info *draw)
{
   bool dirty =
      is_dirty(cmdbuf, RS_RASTERIZER_DISCARD_ENABLE) ||
      is_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
      is_dirty(cmdbuf, RS_DEPTH_BIAS_ENABLE) ||
      is_dirty(cmdbuf, RS_DEPTH_BIAS_FACTORS) ||
      is_dirty(cmdbuf, CB_LOGIC_OP_ENABLE) || is_dirty(cmdbuf, CB_LOGIC_OP) ||
      is_dirty(cmdbuf, CB_ATTACHMENT_COUNT) ||
      is_dirty(cmdbuf, CB_COLOR_WRITE_ENABLES) ||
      is_dirty(cmdbuf, CB_BLEND_ENABLES) ||
      is_dirty(cmdbuf, CB_BLEND_EQUATIONS) ||
      is_dirty(cmdbuf, CB_WRITE_MASKS) ||
      is_dirty(cmdbuf, CB_BLEND_CONSTANTS) ||
      is_dirty(cmdbuf, DS_DEPTH_TEST_ENABLE) ||
      is_dirty(cmdbuf, DS_DEPTH_WRITE_ENABLE) ||
      is_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
      is_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
      is_dirty(cmdbuf, DS_STENCIL_TEST_ENABLE) ||
      is_dirty(cmdbuf, DS_STENCIL_OP) ||
      is_dirty(cmdbuf, DS_STENCIL_COMPARE_MASK) ||
      is_dirty(cmdbuf, DS_STENCIL_WRITE_MASK) ||
      is_dirty(cmdbuf, DS_STENCIL_REFERENCE) ||
      is_dirty(cmdbuf, MS_RASTERIZATION_SAMPLES) ||
      is_dirty(cmdbuf, MS_SAMPLE_MASK) ||
      is_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE) ||
      is_dirty(cmdbuf, MS_ALPHA_TO_ONE_ENABLE) || !cmdbuf->state.gfx.fs.rsd;

   if (!dirty) {
      draw->fs.rsd = cmdbuf->state.gfx.fs.rsd;
      return VK_SUCCESS;
   }

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs = &dyns->rs;
   const struct vk_color_blend_state *cb = &dyns->cb;
   const struct vk_depth_stencil_state *ds = &dyns->ds;
   const struct panvk_shader *fs = cmdbuf->state.gfx.fs.shader;
   const struct pan_shader_info *fs_info = fs ? &fs->info : NULL;
   unsigned bd_count = MAX2(cb->attachment_count, 1);
   bool test_s = has_stencil_att(cmdbuf) && ds->stencil.test_enable;
   bool test_z = has_depth_att(cmdbuf) && ds->depth.test_enable;
   bool writes_z = writes_depth(cmdbuf);
   bool writes_s = writes_stencil(cmdbuf);
   bool needs_fs = fs_required(cmdbuf);

   struct panfrost_ptr ptr = panvk_cmd_alloc_desc_aggregate(
      cmdbuf, PAN_DESC(RENDERER_STATE), PAN_DESC_ARRAY(bd_count, BLEND));
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_renderer_state_packed *rsd = ptr.cpu;
   struct mali_blend_packed *bds = ptr.cpu + pan_size(RENDERER_STATE);
   struct panvk_blend_info binfo = {0};

   mali_ptr fs_code = panvk_shader_get_dev_addr(fs);

   if (fs_info != NULL) {
      panvk_per_arch(blend_emit_descs)(
         dev, dyns, cmdbuf->state.gfx.render.color_attachments.fmts,
         cmdbuf->state.gfx.render.color_attachments.samples, fs_info, fs_code,
         bds, &binfo);
   } else {
      for (unsigned i = 0; i < bd_count; i++) {
         pan_pack(&bds[i], BLEND, cfg) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
         }
      }
   }

   pan_pack(rsd, RENDERER_STATE, cfg) {
      bool alpha_to_coverage = dyns->ms.alpha_to_coverage_enable;

      if (needs_fs) {
         pan_shader_prepare_rsd(fs_info, fs_code, &cfg);

         if (binfo.shader_loads_blend_const) {
            /* Preload the blend constant if the blend shader depends on it. */
            cfg.preload.uniform_count = MAX2(
               cfg.preload.uniform_count,
               DIV_ROUND_UP(256 + sizeof(struct panvk_graphics_sysvals), 8));
         }

         uint8_t rt_written = fs_info->outputs_written >> FRAG_RESULT_DATA0;
         uint8_t rt_mask = cmdbuf->state.gfx.render.bound_attachments &
                           MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS;
         cfg.properties.allow_forward_pixel_to_kill =
            fs_info->fs.can_fpk && !(rt_mask & ~rt_written) &&
            !alpha_to_coverage && !binfo.any_dest_read;

         bool writes_zs = writes_z || writes_s;
         bool zs_always_passes = ds_test_always_passes(cmdbuf);
         bool oq = false; /* TODO: Occlusion queries */

         struct pan_earlyzs_state earlyzs =
            pan_earlyzs_get(pan_earlyzs_analyze(fs_info), writes_zs || oq,
                            alpha_to_coverage, zs_always_passes);

         cfg.properties.pixel_kill_operation = earlyzs.kill;
         cfg.properties.zs_update_operation = earlyzs.update;
      } else {
         cfg.properties.depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION;
         cfg.properties.allow_forward_pixel_to_kill = true;
         cfg.properties.allow_forward_pixel_to_be_killed = true;
         cfg.properties.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
      }

      bool msaa = dyns->ms.rasterization_samples > 1;
      cfg.multisample_misc.multisample_enable = msaa;
      cfg.multisample_misc.sample_mask =
         msaa ? dyns->ms.sample_mask : UINT16_MAX;

      cfg.multisample_misc.depth_function =
         test_z ? translate_compare_func(ds->depth.compare_op)
                : MALI_FUNC_ALWAYS;

      cfg.multisample_misc.depth_write_mask = writes_z;
      cfg.multisample_misc.fixed_function_near_discard =
         !rs->depth_clamp_enable;
      cfg.multisample_misc.fixed_function_far_discard = !rs->depth_clamp_enable;
      cfg.multisample_misc.shader_depth_range_fixed = true;

      cfg.stencil_mask_misc.stencil_enable = test_s;
      cfg.stencil_mask_misc.alpha_to_coverage = alpha_to_coverage;
      cfg.stencil_mask_misc.alpha_test_compare_function = MALI_FUNC_ALWAYS;
      cfg.stencil_mask_misc.front_facing_depth_bias = rs->depth_bias.enable;
      cfg.stencil_mask_misc.back_facing_depth_bias = rs->depth_bias.enable;
      cfg.stencil_mask_misc.single_sampled_lines =
         dyns->ms.rasterization_samples <= 1;

      cfg.depth_units = rs->depth_bias.constant * 2.0f;
      cfg.depth_factor = rs->depth_bias.slope;
      cfg.depth_bias_clamp = rs->depth_bias.clamp;

      cfg.stencil_front.mask = ds->stencil.front.compare_mask;
      cfg.stencil_back.mask = ds->stencil.back.compare_mask;

      cfg.stencil_mask_misc.stencil_mask_front = ds->stencil.front.write_mask;
      cfg.stencil_mask_misc.stencil_mask_back = ds->stencil.back.write_mask;

      cfg.stencil_front.reference_value = ds->stencil.front.reference;
      cfg.stencil_back.reference_value = ds->stencil.back.reference;

      if (test_s) {
         cfg.stencil_front.compare_function =
            translate_compare_func(ds->stencil.front.op.compare);
         cfg.stencil_front.stencil_fail =
            translate_stencil_op(ds->stencil.front.op.fail);
         cfg.stencil_front.depth_fail =
            translate_stencil_op(ds->stencil.front.op.depth_fail);
         cfg.stencil_front.depth_pass =
            translate_stencil_op(ds->stencil.front.op.pass);
         cfg.stencil_back.compare_function =
            translate_compare_func(ds->stencil.back.op.compare);
         cfg.stencil_back.stencil_fail =
            translate_stencil_op(ds->stencil.back.op.fail);
         cfg.stencil_back.depth_fail =
            translate_stencil_op(ds->stencil.back.op.depth_fail);
         cfg.stencil_back.depth_pass =
            translate_stencil_op(ds->stencil.back.op.pass);
      }
   }

   cmdbuf->state.gfx.fs.rsd = ptr.gpu;
   draw->fs.rsd = cmdbuf->state.gfx.fs.rsd;
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_tiler_context(struct panvk_cmd_buffer *cmdbuf,
                                 struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   VkResult result =
      panvk_per_arch(cmd_prepare_tiler_context)(cmdbuf, draw->layer_id);
   if (result != VK_SUCCESS)
      return result;

   draw->tiler_ctx = &batch->tiler.ctx;
   return VK_SUCCESS;
}

static mali_pixel_format
panvk_varying_hw_format(gl_shader_stage stage, gl_varying_slot loc,
                        enum pipe_format pfmt)
{
   switch (loc) {
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
#if PAN_ARCH <= 6
      return (MALI_R16F << 12) | panfrost_get_default_swizzle(1);
#else
      return (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_R000;
#endif
   case VARYING_SLOT_POS:
#if PAN_ARCH <= 6
      return (MALI_SNAP_4 << 12) | panfrost_get_default_swizzle(4);
#else
      return (MALI_SNAP_4 << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
#endif
   default:
      if (pfmt != PIPE_FORMAT_NONE)
         return GENX(panfrost_format_from_pipe_format)(pfmt)->hw;

#if PAN_ARCH >= 7
      return (MALI_CONSTANT << 12) | MALI_RGB_COMPONENT_ORDER_0000;
#else
      return (MALI_CONSTANT << 12) | PAN_V6_SWIZZLE(0, 0, 0, 0);
#endif
   }
}

static VkResult
panvk_draw_prepare_varyings(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_info *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_link *link = &cmdbuf->state.gfx.link;
   struct panfrost_ptr bufs = panvk_cmd_alloc_desc_array(
      cmdbuf, PANVK_VARY_BUF_MAX + 1, ATTRIBUTE_BUFFER);
   if (!bufs.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_attribute_buffer_packed *buf_descs = bufs.cpu;
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   bool writes_point_size =
      vs->info.vs.writes_point_size &&
      ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
   unsigned vertex_count = draw->padded_vertex_count * draw->instance_count;
   mali_ptr psiz_buf = 0;

   for (unsigned i = 0; i < PANVK_VARY_BUF_MAX; i++) {
      unsigned buf_size = vertex_count * link->buf_strides[i];
      mali_ptr buf_addr =
         buf_size ? panvk_cmd_alloc_dev_mem(cmdbuf, varying, buf_size, 64).gpu
                  : 0;
      if (buf_size && !buf_addr)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      pan_pack(&buf_descs[i], ATTRIBUTE_BUFFER, cfg) {
         cfg.stride = link->buf_strides[i];
         cfg.size = buf_size;
         cfg.pointer = buf_addr;
      }

      if (i == PANVK_VARY_BUF_POSITION)
         draw->position = buf_addr;

      if (i == PANVK_VARY_BUF_PSIZ)
         psiz_buf = buf_addr;
   }

   /* We need an empty entry to stop prefetching on Bifrost */
   memset(bufs.cpu + (pan_size(ATTRIBUTE_BUFFER) * PANVK_VARY_BUF_MAX), 0,
          pan_size(ATTRIBUTE_BUFFER));

   if (writes_point_size)
      draw->psiz = psiz_buf;
   else if (ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
            ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
      draw->line_width = cmdbuf->vk.dynamic_graphics_state.rs.line.width;
   else
      draw->line_width = 1.0f;

   draw->varying_bufs = bufs.gpu;
   draw->vs.varyings = panvk_priv_mem_dev_addr(link->vs.attribs);
   draw->fs.varyings = panvk_priv_mem_dev_addr(link->fs.attribs);
   return VK_SUCCESS;
}

static void
panvk_draw_emit_attrib_buf(const struct panvk_draw_info *draw,
                           const struct vk_vertex_binding_state *buf_info,
                           const struct panvk_attrib_buf *buf, void *desc)
{
   mali_ptr addr = buf->address & ~63ULL;
   unsigned size = buf->size + (buf->address & 63);
   unsigned divisor = draw->padded_vertex_count * buf_info->divisor;
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   void *buf_ext = desc + pan_size(ATTRIBUTE_BUFFER);

   /* TODO: support instanced arrays */
   if (draw->instance_count <= 1) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = per_instance ? 0 : buf_info->stride;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (!per_instance) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
         cfg.divisor = draw->padded_vertex_count;
         cfg.stride = buf_info->stride;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (!divisor) {
      /* instance_divisor == 0 means all instances share the same value.
       * Make it a 1D array with a zero stride.
       */
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = 0;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (util_is_power_of_two_or_zero(divisor)) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.stride = buf_info->stride;
         cfg.pointer = addr;
         cfg.size = size;
         cfg.divisor_r = __builtin_ctz(divisor);
      }
   } else {
      unsigned divisor_r = 0, divisor_e = 0;
      unsigned divisor_num =
         panfrost_compute_magic_divisor(divisor, &divisor_r, &divisor_e);
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.stride = buf_info->stride;
         cfg.pointer = addr;
         cfg.size = size;
         cfg.divisor_r = divisor_r;
         cfg.divisor_e = divisor_e;
      }

      pan_pack(buf_ext, ATTRIBUTE_BUFFER_CONTINUATION_NPOT, cfg) {
         cfg.divisor_numerator = divisor_num;
         cfg.divisor = buf_info->divisor;
      }

      buf_ext = NULL;
   }

   /* If the buffer extension wasn't used, memset(0) */
   if (buf_ext)
      memset(buf_ext, 0, pan_size(ATTRIBUTE_BUFFER));
}

static void
panvk_draw_emit_attrib(const struct panvk_draw_info *draw,
                       const struct vk_vertex_attribute_state *attrib_info,
                       const struct vk_vertex_binding_state *buf_info,
                       const struct panvk_attrib_buf *buf, void *desc)
{
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   enum pipe_format f = vk_format_to_pipe_format(attrib_info->format);
   unsigned buf_idx = attrib_info->binding;

   pan_pack(desc, ATTRIBUTE, cfg) {
      cfg.buffer_index = buf_idx * 2;
      cfg.offset = attrib_info->offset + (buf->address & 63);
      cfg.offset_enable = true;

      if (per_instance)
         cfg.offset += draw->first_instance * buf_info->stride;

      cfg.format = GENX(panfrost_format_from_pipe_format)(f)->hw;
   }
}

static VkResult
panvk_draw_prepare_vs_attribs(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_info *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;
   unsigned num_imgs = vs->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_IMG];
   unsigned num_vs_attribs = util_last_bit(vi->attributes_valid);
   unsigned num_vbs = util_last_bit(vi->bindings_valid);
   unsigned attrib_count =
      num_imgs ? MAX_VS_ATTRIBS + num_imgs : num_vs_attribs;
   bool dirty =
      is_dirty(cmdbuf, VI) || is_dirty(cmdbuf, VI_BINDINGS_VALID) ||
      is_dirty(cmdbuf, VI_BINDING_STRIDES) ||
      (num_imgs && !cmdbuf->state.gfx.vs.desc.img_attrib_table) ||
      (cmdbuf->state.gfx.vb.count && !cmdbuf->state.gfx.vs.attrib_bufs) ||
      (attrib_count && !cmdbuf->state.gfx.vs.attribs);

   if (!dirty)
      return VK_SUCCESS;

   unsigned attrib_buf_count = (num_vbs + num_imgs) * 2;
   struct panfrost_ptr bufs = panvk_cmd_alloc_desc_array(
      cmdbuf, attrib_buf_count + 1, ATTRIBUTE_BUFFER);
   struct mali_attribute_buffer_packed *attrib_buf_descs = bufs.cpu;
   struct panfrost_ptr attribs =
      panvk_cmd_alloc_desc_array(cmdbuf, attrib_count, ATTRIBUTE);
   struct mali_attribute_packed *attrib_descs = attribs.cpu;

   if (!bufs.gpu || (attrib_count && !attribs.gpu))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   for (unsigned i = 0; i < num_vbs; i++) {
      if (vi->bindings_valid & BITFIELD_BIT(i)) {
         panvk_draw_emit_attrib_buf(draw, &vi->bindings[i],
                                    &cmdbuf->state.gfx.vb.bufs[i],
                                    &attrib_buf_descs[i * 2]);
      } else {
         memset(&attrib_buf_descs[i * 2], 0, sizeof(*attrib_buf_descs) * 2);
      }
   }

   for (unsigned i = 0; i < num_vs_attribs; i++) {
      if (vi->attributes_valid & BITFIELD_BIT(i)) {
         unsigned buf_idx = vi->attributes[i].binding;
         panvk_draw_emit_attrib(
            draw, &vi->attributes[i], &vi->bindings[buf_idx],
            &cmdbuf->state.gfx.vb.bufs[buf_idx], &attrib_descs[i]);
      } else {
         memset(&attrib_descs[i], 0, sizeof(attrib_descs[0]));
      }
   }

   /* A NULL entry is needed to stop prefecting on Bifrost */
   memset(bufs.cpu + (pan_size(ATTRIBUTE_BUFFER) * attrib_buf_count), 0,
          pan_size(ATTRIBUTE_BUFFER));

   cmdbuf->state.gfx.vs.attrib_bufs = bufs.gpu;
   cmdbuf->state.gfx.vs.attribs = attribs.gpu;

   if (num_imgs) {
      cmdbuf->state.gfx.vs.desc.img_attrib_table =
         attribs.gpu + (MAX_VS_ATTRIBS * pan_size(ATTRIBUTE));
      cmdbuf->state.gfx.vs.desc.tables[PANVK_BIFROST_DESC_TABLE_IMG] =
         bufs.gpu + (num_vbs * pan_size(ATTRIBUTE_BUFFER) * 2);
   }

   return VK_SUCCESS;
}

static void
panvk_draw_prepare_attributes(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_info *draw)
{
   panvk_draw_prepare_vs_attribs(cmdbuf, draw);
   draw->vs.attributes = cmdbuf->state.gfx.vs.attribs;
   draw->vs.attribute_bufs = cmdbuf->state.gfx.vs.attrib_bufs;
}

static void
panvk_emit_viewport(const struct vk_viewport_state *vp, void *vpd)
{
   assert(vp->viewport_count == 1);

   const VkViewport *viewport = &vp->viewports[0];
   const VkRect2D *scissor = &vp->scissors[0];

   /* The spec says "width must be greater than 0.0" */
   assert(viewport->width >= 0);
   int minx = (int)viewport->x;
   int maxx = (int)(viewport->x + viewport->width);

   /* Viewport height can be negative */
   int miny = MIN2((int)viewport->y, (int)(viewport->y + viewport->height));
   int maxy = MAX2((int)viewport->y, (int)(viewport->y + viewport->height));

   assert(scissor->offset.x >= 0 && scissor->offset.y >= 0);
   minx = MAX2(scissor->offset.x, minx);
   miny = MAX2(scissor->offset.y, miny);
   maxx = MIN2(scissor->offset.x + scissor->extent.width, maxx);
   maxy = MIN2(scissor->offset.y + scissor->extent.height, maxy);

   /* Make sure we don't end up with a max < min when width/height is 0 */
   maxx = maxx > minx ? maxx - 1 : maxx;
   maxy = maxy > miny ? maxy - 1 : maxy;

   /* Clamp viewport scissor to valid range */
   minx = CLAMP(minx, 0, UINT16_MAX);
   maxx = CLAMP(maxx, 0, UINT16_MAX);
   miny = CLAMP(miny, 0, UINT16_MAX);
   maxy = CLAMP(maxy, 0, UINT16_MAX);

   assert(viewport->minDepth >= 0.0f && viewport->minDepth <= 1.0f);
   assert(viewport->maxDepth >= 0.0f && viewport->maxDepth <= 1.0f);

   pan_pack(vpd, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
      cfg.minimum_z = MIN2(viewport->minDepth, viewport->maxDepth);
      cfg.maximum_z = MAX2(viewport->minDepth, viewport->maxDepth);
   }
}

static VkResult
panvk_draw_prepare_viewport(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_info *draw)
{
   /* When rasterizerDiscardEnable is active, it is allowed to have viewport and
    * scissor disabled.
    * As a result, we define an empty one.
    */
   if (!cmdbuf->state.gfx.vpd || is_dirty(cmdbuf, VP_VIEWPORTS) ||
       is_dirty(cmdbuf, VP_SCISSORS)) {
      struct panfrost_ptr vp = panvk_cmd_alloc_desc(cmdbuf, VIEWPORT);
      if (!vp.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      const struct vk_viewport_state *vps =
         &cmdbuf->vk.dynamic_graphics_state.vp;

      if (vps->viewport_count > 0)
         panvk_emit_viewport(vps, vp.cpu);
      cmdbuf->state.gfx.vpd = vp.gpu;
   }

   draw->viewport = cmdbuf->state.gfx.vpd;
   return VK_SUCCESS;
}

static void
panvk_emit_vertex_dcd(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_info *draw, void *dcd)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;

   pan_pack(dcd, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(vs->rsd);
      cfg.attributes = draw->vs.attributes;
      cfg.attribute_buffers = draw->vs.attribute_bufs;
      cfg.varyings = draw->vs.varyings;
      cfg.varying_buffers = draw->varying_bufs;
      cfg.thread_storage = draw->tls;
      cfg.offset_start = draw->offset_start;
      cfg.instance_size =
         draw->instance_count > 1 ? draw->padded_vertex_count : 1;
      cfg.uniform_buffers = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = draw->push_uniforms;
      cfg.textures = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];
   }
}

static VkResult
panvk_draw_prepare_vertex_job(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct panfrost_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   util_dynarray_append(&batch->jobs, void *, ptr.cpu);
   draw->jobs.vertex = ptr;

   memcpy(pan_section_ptr(ptr.cpu, COMPUTE_JOB, INVOCATION), &draw->invocation,
          pan_size(INVOCATION));

   pan_section_pack(ptr.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = 5;
   }

   panvk_emit_vertex_dcd(cmdbuf, draw,
                         pan_section_ptr(ptr.cpu, COMPUTE_JOB, DRAW));
   return VK_SUCCESS;
}

static enum mali_draw_mode
translate_prim_topology(VkPrimitiveTopology in)
{
   /* Test VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA separately, as it's not
    * part of the VkPrimitiveTopology enum.
    */
   if (in == VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA)
      return MALI_DRAW_MODE_TRIANGLES;

   switch (in) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MALI_DRAW_MODE_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return MALI_DRAW_MODE_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MALI_DRAW_MODE_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return MALI_DRAW_MODE_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return MALI_DRAW_MODE_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return MALI_DRAW_MODE_TRIANGLE_FAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
   default:
      unreachable("Invalid primitive type");
   }
}

static void
panvk_emit_tiler_primitive(struct panvk_cmd_buffer *cmdbuf,
                           const struct panvk_draw_info *draw, void *prim)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   bool writes_point_size =
      vs->info.vs.writes_point_size &&
      ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
   bool secondary_shader = vs->info.vs.secondary_enable && fs_required(cmdbuf);

   pan_pack(prim, PRIMITIVE, cfg) {
      cfg.draw_mode = translate_prim_topology(ia->primitive_topology);
      if (writes_point_size)
         cfg.point_size_array_format = MALI_POINT_SIZE_ARRAY_FORMAT_FP16;

      cfg.first_provoking_vertex = true;
      if (ia->primitive_restart_enable)
         cfg.primitive_restart = MALI_PRIMITIVE_RESTART_IMPLICIT;
      cfg.job_task_split = 6;

      if (draw->index_size) {
         cfg.index_count = draw->index_count;
         cfg.indices = draw->indices;
         cfg.base_vertex_offset = draw->vertex_offset - draw->offset_start;

         switch (draw->index_size) {
         case 4:
            cfg.index_type = MALI_INDEX_TYPE_UINT32;
            break;
         case 2:
            cfg.index_type = MALI_INDEX_TYPE_UINT16;
            break;
         case 1:
            cfg.index_type = MALI_INDEX_TYPE_UINT8;
            break;
         default:
            unreachable("Invalid index size");
         }
      } else {
         cfg.index_count = draw->vertex_count;
         cfg.index_type = MALI_INDEX_TYPE_NONE;
      }

      cfg.secondary_shader = secondary_shader;
   }
}

static void
panvk_emit_tiler_primitive_size(struct panvk_cmd_buffer *cmdbuf,
                                const struct panvk_draw_info *draw,
                                void *primsz)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   bool writes_point_size =
      vs->info.vs.writes_point_size &&
      ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

   pan_pack(primsz, PRIMITIVE_SIZE, cfg) {
      if (writes_point_size) {
         cfg.size_array = draw->psiz;
      } else {
         cfg.constant = draw->line_width;
      }
   }
}

static void
panvk_emit_tiler_dcd(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_draw_info *draw, void *dcd)
{
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;

   pan_pack(dcd, DRAW, cfg) {
      cfg.front_face_ccw = rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE;
      cfg.cull_front_face = (rs->cull_mode & VK_CULL_MODE_FRONT_BIT) != 0;
      cfg.cull_back_face = (rs->cull_mode & VK_CULL_MODE_BACK_BIT) != 0;
      cfg.position = draw->position;
      cfg.state = draw->fs.rsd;
      cfg.attributes = fs_desc_state->img_attrib_table;
      cfg.attribute_buffers =
         fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_IMG];
      cfg.viewport = draw->viewport;
      cfg.varyings = draw->fs.varyings;
      cfg.varying_buffers = cfg.varyings ? draw->varying_bufs : 0;
      cfg.thread_storage = draw->tls;

      /* For all primitives but lines DRAW.flat_shading_vertex must
       * be set to 0 and the provoking vertex is selected with the
       * PRIMITIVE.first_provoking_vertex field.
       */
      if (ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
          ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
         cfg.flat_shading_vertex = true;

      cfg.offset_start = draw->offset_start;
      cfg.instance_size =
         draw->instance_count > 1 ? draw->padded_vertex_count : 1;
      cfg.uniform_buffers = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = draw->push_uniforms;
      cfg.textures = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];

      /* TODO: occlusion queries */
   }
}

static VkResult
panvk_draw_prepare_tiler_job(struct panvk_cmd_buffer *cmdbuf,
                             struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader *fs = cmdbuf->state.gfx.fs.shader;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panfrost_ptr ptr;
   VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
      cmdbuf, fs, &cmdbuf->state.gfx.desc_state, fs_desc_state, 0, &ptr);

   if (result != VK_SUCCESS)
      return result;

   if (ptr.cpu)
      util_dynarray_append(&batch->jobs, void *, ptr.cpu);

   draw->jobs.frag_copy_desc = ptr;

   ptr = panvk_cmd_alloc_desc(cmdbuf, TILER_JOB);
   util_dynarray_append(&batch->jobs, void *, ptr.cpu);
   draw->jobs.tiler = ptr;

   memcpy(pan_section_ptr(ptr.cpu, TILER_JOB, INVOCATION), &draw->invocation,
          pan_size(INVOCATION));

   panvk_emit_tiler_primitive(cmdbuf, draw,
                              pan_section_ptr(ptr.cpu, TILER_JOB, PRIMITIVE));

   panvk_emit_tiler_primitive_size(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, TILER_JOB, PRIMITIVE_SIZE));

   panvk_emit_tiler_dcd(cmdbuf, draw,
                        pan_section_ptr(ptr.cpu, TILER_JOB, DRAW));

   pan_section_pack(ptr.cpu, TILER_JOB, TILER, cfg) {
      cfg.address = PAN_ARCH >= 9 ? draw->tiler_ctx->valhall.desc
                                  : draw->tiler_ctx->bifrost.desc;
   }

   pan_section_pack(ptr.cpu, TILER_JOB, PADDING, padding)
      ;

   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_idvs_job(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct panfrost_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, INDEXED_VERTEX_JOB);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   util_dynarray_append(&batch->jobs, void *, ptr.cpu);
   draw->jobs.idvs = ptr;

   memcpy(pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, INVOCATION),
          &draw->invocation, pan_size(INVOCATION));

   panvk_emit_tiler_primitive(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, PRIMITIVE));

   panvk_emit_tiler_primitive_size(
      cmdbuf, draw,
      pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, PRIMITIVE_SIZE));

   pan_section_pack(ptr.cpu, INDEXED_VERTEX_JOB, TILER, cfg) {
      cfg.address = PAN_ARCH >= 9 ? draw->tiler_ctx->valhall.desc
                                  : draw->tiler_ctx->bifrost.desc;
   }

   pan_section_pack(ptr.cpu, INDEXED_VERTEX_JOB, PADDING, _) {
   }

   panvk_emit_tiler_dcd(
      cmdbuf, draw,
      pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, FRAGMENT_DRAW));

   panvk_emit_vertex_dcd(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, VERTEX_DRAW));
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_vs_copy_desc_job(struct panvk_cmd_buffer *cmdbuf,
                                    struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;
   unsigned num_vbs = util_last_bit(vi->bindings_valid);
   struct panfrost_ptr ptr;
   VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
      cmdbuf, vs, &cmdbuf->state.gfx.desc_state, vs_desc_state,
      num_vbs * pan_size(ATTRIBUTE_BUFFER) * 2, &ptr);
   if (result != VK_SUCCESS)
      return result;

   if (ptr.cpu)
      util_dynarray_append(&batch->jobs, void *, ptr.cpu);

   draw->jobs.vertex_copy_desc = ptr;
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_fs_copy_desc_job(struct panvk_cmd_buffer *cmdbuf,
                                    struct panvk_draw_info *draw)
{
   const struct panvk_shader *fs = cmdbuf->state.gfx.fs.shader;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct panfrost_ptr ptr;
   VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
      cmdbuf, fs, &cmdbuf->state.gfx.desc_state, fs_desc_state, 0, &ptr);

   if (result != VK_SUCCESS)
      return result;

   if (ptr.cpu)
      util_dynarray_append(&batch->jobs, void *, ptr.cpu);

   draw->jobs.frag_copy_desc = ptr;
   return VK_SUCCESS;
}

void
panvk_per_arch(cmd_preload_fb_after_batch_split)(struct panvk_cmd_buffer *cmdbuf)
{
   for (unsigned i = 0; i < cmdbuf->state.gfx.render.fb.info.rt_count; i++) {
      if (cmdbuf->state.gfx.render.fb.info.rts[i].view) {
         cmdbuf->state.gfx.render.fb.info.rts[i].clear = false;
         cmdbuf->state.gfx.render.fb.info.rts[i].preload = true;
      }
   }

   if (cmdbuf->state.gfx.render.fb.info.zs.view.zs) {
      cmdbuf->state.gfx.render.fb.info.zs.clear.z = false;
      cmdbuf->state.gfx.render.fb.info.zs.preload.z = true;
   }

   if (cmdbuf->state.gfx.render.fb.info.zs.view.s ||
       (cmdbuf->state.gfx.render.fb.info.zs.view.zs &&
        util_format_is_depth_and_stencil(
           cmdbuf->state.gfx.render.fb.info.zs.view.zs->format))) {
      cmdbuf->state.gfx.render.fb.info.zs.clear.s = false;
      cmdbuf->state.gfx.render.fb.info.zs.preload.s = true;
   }
}

static VkResult
panvk_cmd_prepare_draw_link_shaders(struct panvk_cmd_buffer *cmd)
{
   struct panvk_cmd_graphics_state *gfx = &cmd->state.gfx;

   if (gfx->linked)
      return VK_SUCCESS;

   VkResult result = panvk_per_arch(link_shaders)(
      &cmd->desc_pool, gfx->vs.shader, gfx->fs.shader, &gfx->link);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return result;
   }

   gfx->linked = true;
   return VK_SUCCESS;
}

static void
panvk_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *fs = cmdbuf->state.gfx.fs.shader;
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   uint32_t layer_count = cmdbuf->state.gfx.render.layer_count;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;
   bool idvs = vs->info.vs.idvs;
   VkResult result;

   /* If there's no vertex shader, we can skip the draw. */
   if (!panvk_priv_mem_dev_addr(vs->rsd))
      return;

   /* There are only 16 bits in the descriptor for the job ID. Each job has a
    * pilot shader dealing with descriptor copies, and we need one
    * <vertex,tiler> pair per draw.
    */
   if (batch->vtc_jc.job_index + (4 * layer_count) >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_preload_fb_after_batch_split)(cmdbuf);
      batch = panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   result = panvk_cmd_prepare_draw_link_shaders(cmdbuf);
   if (result != VK_SUCCESS)
      return;

   if (!rs->rasterizer_discard_enable) {
      result = panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);
      if (result != VK_SUCCESS)
         return;
   }

   result = panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true);
   if (result != VK_SUCCESS)
      return;

   panvk_draw_prepare_attributes(cmdbuf, draw);

   uint32_t used_set_mask =
      vs->desc_info.used_set_mask | (fs ? fs->desc_info.used_set_mask : 0);

   result =
      panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state, used_set_mask);
   if (result != VK_SUCCESS)
      return;

   result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
      cmdbuf, &cmdbuf->state.gfx.desc_state, vs, vs_desc_state);
   if (result != VK_SUCCESS)
      return;

   panvk_draw_prepare_vs_copy_desc_job(cmdbuf, draw);

   unsigned copy_desc_job_id =
      draw->jobs.vertex_copy_desc.gpu
         ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false,
                          0, 0, &draw->jobs.vertex_copy_desc, false)
         : 0;

   bool vs_writes_pos =
      cmdbuf->state.gfx.link.buf_strides[PANVK_VARY_BUF_POSITION] > 0;
   bool needs_tiling = !rs->rasterizer_discard_enable && vs_writes_pos;

   /* No need to setup the FS desc tables if the FS is not executed. */
   if (needs_tiling && fs_required(cmdbuf)) {
      result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
         cmdbuf, &cmdbuf->state.gfx.desc_state, fs, fs_desc_state);
      if (result != VK_SUCCESS)
         return;

      result = panvk_draw_prepare_fs_copy_desc_job(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return;

      if (draw->jobs.frag_copy_desc.gpu) {
         /* We don't need to add frag_copy_desc as a dependency because the
          * tiler job doesn't execute the fragment shader, the fragment job
          * will, and the tiler/fragment synchronization happens at the batch
          * level. */
         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false, 0,
                        0, &draw->jobs.frag_copy_desc, false);
      }
   }

   /* TODO: indexed draws */
   draw->tls = batch->tls.gpu;
   draw->fb = batch->fb.desc.gpu;

   panfrost_pack_work_groups_compute(&draw->invocation, 1, draw->vertex_range,
                                     draw->instance_count, 1, 1, 1, true,
                                     false);

   result = panvk_draw_prepare_fs_rsd(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   result = panvk_draw_prepare_viewport(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   batch->tlsinfo.tls.size = MAX3(vs->info.tls_size, fs ? fs->info.tls_size : 0,
                                  batch->tlsinfo.tls.size);

   for (uint32_t i = 0; i < layer_count; i++) {
      draw->layer_id = i;
      result = panvk_draw_prepare_varyings(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return;

      result = panvk_cmd_prepare_draw_sysvals(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return;

      cmdbuf->state.gfx.push_uniforms = panvk_per_arch(
         cmd_prepare_push_uniforms)(cmdbuf, &cmdbuf->state.gfx.sysvals,
                                    sizeof(cmdbuf->state.gfx.sysvals));
      if (!cmdbuf->state.gfx.push_uniforms)
         return;

      draw->push_uniforms = cmdbuf->state.gfx.push_uniforms;
      result = panvk_draw_prepare_tiler_context(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return;

      if (idvs) {
         result = panvk_draw_prepare_idvs_job(cmdbuf, draw);
         if (result != VK_SUCCESS)
            return;

         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_INDEXED_VERTEX, false,
                        false, 0, copy_desc_job_id, &draw->jobs.idvs, false);
      } else {
         result = panvk_draw_prepare_vertex_job(cmdbuf, draw);
         if (result != VK_SUCCESS)
            return;

         unsigned vjob_id =
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_VERTEX, false, false,
                           0, copy_desc_job_id, &draw->jobs.vertex, false);

         if (needs_tiling) {
            panvk_draw_prepare_tiler_job(cmdbuf, draw);
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_TILER, false, false,
                           vjob_id, 0, &draw->jobs.tiler, false);
         }
      }
   }

   /* Clear the dirty flags all at once */
   vk_dynamic_graphics_state_clear_dirty(&cmdbuf->vk.dynamic_graphics_state);
   cmdbuf->state.gfx.dirty = 0;
}

static unsigned
padded_vertex_count(struct panvk_cmd_buffer *cmdbuf, uint32_t vertex_count,
                    uint32_t instance_count)
{
   if (instance_count == 1)
      return vertex_count;

   bool idvs = cmdbuf->state.gfx.vs.shader->info.vs.idvs;

   /* Index-Driven Vertex Shading requires different instances to
    * have different cache lines for position results. Each vertex
    * position is 16 bytes and the Mali cache line is 64 bytes, so
    * the instance count must be aligned to 4 vertices.
    */
   if (idvs)
      vertex_count = ALIGN_POT(vertex_count, 4);

   return panfrost_padded_vertex_count(vertex_count);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                        uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || vertexCount == 0)
      return;

   struct panvk_draw_info draw = {
      .first_vertex = firstVertex,
      .vertex_count = vertexCount,
      .vertex_range = vertexCount,
      .first_instance = firstInstance,
      .instance_count = instanceCount,
      .padded_vertex_count =
         padded_vertex_count(cmdbuf, vertexCount, instanceCount),
      .offset_start = firstVertex,
   };

   panvk_cmd_draw(cmdbuf, &draw);
}

static void
panvk_index_minmax_search(struct panvk_cmd_buffer *cmdbuf, uint32_t start,
                          uint32_t count, bool restart, uint32_t *min,
                          uint32_t *max)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   void *ptr =
      cmdbuf->state.gfx.ib.buffer->host_ptr + cmdbuf->state.gfx.ib.offset;

   assert(cmdbuf->state.gfx.ib.buffer);
   assert(cmdbuf->state.gfx.ib.buffer->bo);
   assert(cmdbuf->state.gfx.ib.buffer->host_ptr);

   if (!(instance->debug_flags & PANVK_DEBUG_NO_KNOWN_WARN)) {
      fprintf(
         stderr,
         "WARNING: Crawling index buffers from the CPU isn't valid in Vulkan\n");
   }

   *max = 0;

   /* TODO: Use panfrost_minmax_cache */
   /* TODO: Read full cacheline of data to mitigate the uncached
    * mapping slowness.
    */
   switch (cmdbuf->state.gfx.ib.index_size * 8) {
#define MINMAX_SEARCH_CASE(sz)                                                 \
   case sz: {                                                                  \
      uint##sz##_t *indices = ptr;                                             \
      *min = UINT##sz##_MAX;                                                   \
      for (uint32_t i = 0; i < count; i++) {                                   \
         if (restart && indices[i + start] == UINT##sz##_MAX)                  \
            continue;                                                          \
         *min = MIN2(indices[i + start], *min);                                \
         *max = MAX2(indices[i + start], *max);                                \
      }                                                                        \
      break;                                                                   \
   }
      MINMAX_SEARCH_CASE(32)
      MINMAX_SEARCH_CASE(16)
      MINMAX_SEARCH_CASE(8)
#undef MINMAX_SEARCH_CASE
   default:
      unreachable("Invalid index size");
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexed)(VkCommandBuffer commandBuffer,
                               uint32_t indexCount, uint32_t instanceCount,
                               uint32_t firstIndex, int32_t vertexOffset,
                               uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   uint32_t min_vertex, max_vertex;

   if (instanceCount == 0 || indexCount == 0)
      return;

   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   bool primitive_restart = ia->primitive_restart_enable;

   panvk_index_minmax_search(cmdbuf, firstIndex, indexCount, primitive_restart,
                             &min_vertex, &max_vertex);

   unsigned vertex_range = max_vertex - min_vertex + 1;
   struct panvk_draw_info draw = {
      .index_size = cmdbuf->state.gfx.ib.index_size,
      .first_index = firstIndex,
      .index_count = indexCount,
      .vertex_offset = vertexOffset,
      .first_instance = firstInstance,
      .instance_count = instanceCount,
      .vertex_range = vertex_range,
      .vertex_count = indexCount + abs(vertexOffset),
      .padded_vertex_count =
         padded_vertex_count(cmdbuf, vertex_range, instanceCount),
      .offset_start = min_vertex + vertexOffset,
      .indices = panvk_buffer_gpu_ptr(cmdbuf->state.gfx.ib.buffer,
                                      cmdbuf->state.gfx.ib.offset) +
                 (firstIndex * cmdbuf->state.gfx.ib.index_size),
   };

   panvk_cmd_draw(cmdbuf, &draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirect)(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                                VkDeviceSize offset, uint32_t drawCount,
                                uint32_t stride)
{
   panvk_stub();
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirect)(VkCommandBuffer commandBuffer,
                                       VkBuffer _buffer, VkDeviceSize offset,
                                       uint32_t drawCount, uint32_t stride)
{
   panvk_stub();
}

static void
panvk_cmd_begin_rendering_init_state(struct panvk_cmd_buffer *cmdbuf,
                                     const VkRenderingInfo *pRenderingInfo)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   uint32_t att_width = 0, att_height = 0;

   cmdbuf->state.gfx.render.flags = pRenderingInfo->flags;

   /* Resuming from a suspended pass, the state should be unchanged. */
   if (cmdbuf->state.gfx.render.flags & VK_RENDERING_RESUMING_BIT)
      return;

   cmdbuf->state.gfx.render.fb.bo_count = 0;
   memset(cmdbuf->state.gfx.render.fb.bos, 0,
          sizeof(cmdbuf->state.gfx.render.fb.bos));
   memset(cmdbuf->state.gfx.render.fb.crc_valid, 0,
          sizeof(cmdbuf->state.gfx.render.fb.crc_valid));
   memset(&cmdbuf->state.gfx.render.color_attachments, 0,
          sizeof(cmdbuf->state.gfx.render.color_attachments));
   memset(&cmdbuf->state.gfx.render.z_attachment, 0,
          sizeof(cmdbuf->state.gfx.render.z_attachment));
   memset(&cmdbuf->state.gfx.render.s_attachment, 0,
          sizeof(cmdbuf->state.gfx.render.s_attachment));
   cmdbuf->state.gfx.render.bound_attachments = 0;

   cmdbuf->state.gfx.render.layer_count = pRenderingInfo->layerCount;
   *fbinfo = (struct pan_fb_info){
      .tile_buf_budget = panfrost_query_optimal_tib_size(phys_dev->model),
      .nr_samples = 1,
      .rt_count = pRenderingInfo->colorAttachmentCount,
   };

   assert(pRenderingInfo->colorAttachmentCount <= ARRAY_SIZE(fbinfo->rts));

   for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      const VkRenderingAttachmentInfo *att =
         &pRenderingInfo->pColorAttachments[i];
      VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);

      if (!iview)
         continue;

      struct panvk_image *img =
         container_of(iview->vk.image, struct panvk_image, vk);
      const VkExtent3D iview_size = iview->vk.extent;

      cmdbuf->state.gfx.render.bound_attachments |=
         MESA_VK_RP_ATTACHMENT_COLOR_BIT(i);
      cmdbuf->state.gfx.render.color_attachments.iviews[i] = iview;
      cmdbuf->state.gfx.render.color_attachments.fmts[i] = iview->vk.format;
      cmdbuf->state.gfx.render.color_attachments.samples[i] = img->vk.samples;
      att_width = MAX2(iview_size.width, att_width);
      att_height = MAX2(iview_size.height, att_height);

      cmdbuf->state.gfx.render.fb.bos[cmdbuf->state.gfx.render.fb.bo_count++] =
         img->bo;
      fbinfo->rts[i].view = &iview->pview;
      fbinfo->rts[i].crc_valid = &cmdbuf->state.gfx.render.fb.crc_valid[i];
      fbinfo->nr_samples =
         MAX2(fbinfo->nr_samples, pan_image_view_get_nr_samples(&iview->pview));

      if (att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
         enum pipe_format fmt = vk_format_to_pipe_format(iview->vk.format);
         union pipe_color_union *col =
            (union pipe_color_union *)&att->clearValue.color;

         fbinfo->rts[i].clear = true;
         pan_pack_color(phys_dev->formats.blendable, fbinfo->rts[i].clear_value,
                        col, fmt, false);
      } else if (att->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
         fbinfo->rts[i].preload = true;
      }

      if (att->resolveMode != VK_RESOLVE_MODE_NONE) {
         struct panvk_resolve_attachment *resolve_info =
            &cmdbuf->state.gfx.render.color_attachments.resolve[i];
         VK_FROM_HANDLE(panvk_image_view, resolve_iview, att->resolveImageView);

         resolve_info->mode = att->resolveMode;
         resolve_info->dst_iview = resolve_iview;
      }
   }

   if (pRenderingInfo->pDepthAttachment &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) {
      const VkRenderingAttachmentInfo *att = pRenderingInfo->pDepthAttachment;
      VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);
      struct panvk_image *img =
         container_of(iview->vk.image, struct panvk_image, vk);
      const VkExtent3D iview_size = iview->vk.extent;

      if (iview->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
         cmdbuf->state.gfx.render.bound_attachments |=
            MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
         att_width = MAX2(iview_size.width, att_width);
         att_height = MAX2(iview_size.height, att_height);

         cmdbuf->state.gfx.render.fb
            .bos[cmdbuf->state.gfx.render.fb.bo_count++] = img->bo;
         fbinfo->zs.view.zs = &iview->pview;
         fbinfo->nr_samples = MAX2(
            fbinfo->nr_samples, pan_image_view_get_nr_samples(&iview->pview));
         cmdbuf->state.gfx.render.z_attachment.iview = iview;

         if (vk_format_has_stencil(img->vk.format))
            fbinfo->zs.preload.s = true;

         if (att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            fbinfo->zs.clear.z = true;
            fbinfo->zs.clear_value.depth = att->clearValue.depthStencil.depth;
         } else if (att->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
            fbinfo->zs.preload.z = true;
         }

         if (att->resolveMode != VK_RESOLVE_MODE_NONE) {
            struct panvk_resolve_attachment *resolve_info =
               &cmdbuf->state.gfx.render.z_attachment.resolve;
            VK_FROM_HANDLE(panvk_image_view, resolve_iview,
                           att->resolveImageView);

            resolve_info->mode = att->resolveMode;
            resolve_info->dst_iview = resolve_iview;
         }
      }
   }

   if (pRenderingInfo->pStencilAttachment &&
       pRenderingInfo->pStencilAttachment->imageView != VK_NULL_HANDLE) {
      const VkRenderingAttachmentInfo *att = pRenderingInfo->pStencilAttachment;
      VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);
      struct panvk_image *img =
         container_of(iview->vk.image, struct panvk_image, vk);
      const VkExtent3D iview_size = iview->vk.extent;

      if (iview->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
         cmdbuf->state.gfx.render.bound_attachments |=
            MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
         att_width = MAX2(iview_size.width, att_width);
         att_height = MAX2(iview_size.height, att_height);

         cmdbuf->state.gfx.render.fb
            .bos[cmdbuf->state.gfx.render.fb.bo_count++] = img->bo;

         if (drm_is_afbc(img->pimage.layout.modifier)) {
            assert(fbinfo->zs.view.zs == &iview->pview || !fbinfo->zs.view.zs);
            fbinfo->zs.view.zs = &iview->pview;
         } else {
            fbinfo->zs.view.s =
               &iview->pview != fbinfo->zs.view.zs ? &iview->pview : NULL;
         }

         fbinfo->zs.view.s =
            &iview->pview != fbinfo->zs.view.zs ? &iview->pview : NULL;
         fbinfo->nr_samples = MAX2(
            fbinfo->nr_samples, pan_image_view_get_nr_samples(&iview->pview));
         cmdbuf->state.gfx.render.s_attachment.iview = iview;

         if (vk_format_has_depth(img->vk.format)) {
            assert(fbinfo->zs.view.zs == NULL ||
                   &iview->pview == fbinfo->zs.view.zs);
            fbinfo->zs.view.zs = &iview->pview;

            fbinfo->zs.preload.s = false;
            fbinfo->zs.clear.s = false;
            if (!fbinfo->zs.clear.z)
               fbinfo->zs.preload.z = true;
         }

         if (att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            fbinfo->zs.clear.s = true;
            fbinfo->zs.clear_value.stencil =
               att->clearValue.depthStencil.stencil;
         } else if (att->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
            fbinfo->zs.preload.s = true;
         }

         if (att->resolveMode != VK_RESOLVE_MODE_NONE) {
            struct panvk_resolve_attachment *resolve_info =
               &cmdbuf->state.gfx.render.s_attachment.resolve;
            VK_FROM_HANDLE(panvk_image_view, resolve_iview,
                           att->resolveImageView);

            resolve_info->mode = att->resolveMode;
            resolve_info->dst_iview = resolve_iview;
         }
      }
   }

   if (fbinfo->zs.view.zs) {
      const struct util_format_description *fdesc =
         util_format_description(fbinfo->zs.view.zs->format);
      bool needs_depth = fbinfo->zs.clear.z | fbinfo->zs.preload.z |
                         util_format_has_depth(fdesc);
      bool needs_stencil = fbinfo->zs.clear.s | fbinfo->zs.preload.s |
                           util_format_has_stencil(fdesc);
      enum pipe_format new_fmt =
         util_format_get_blocksize(fbinfo->zs.view.zs->format) == 4
            ? PIPE_FORMAT_Z24_UNORM_S8_UINT
            : PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;

      if (needs_depth && needs_stencil &&
          fbinfo->zs.view.zs->format != new_fmt) {
         cmdbuf->state.gfx.render.zs_pview = *fbinfo->zs.view.zs;
         cmdbuf->state.gfx.render.zs_pview.format = new_fmt;
         fbinfo->zs.view.zs = &cmdbuf->state.gfx.render.zs_pview;
      }
   }

   fbinfo->extent.minx = pRenderingInfo->renderArea.offset.x;
   fbinfo->extent.maxx = pRenderingInfo->renderArea.offset.x +
                         pRenderingInfo->renderArea.extent.width - 1;
   fbinfo->extent.miny = pRenderingInfo->renderArea.offset.y;
   fbinfo->extent.maxy = pRenderingInfo->renderArea.offset.y +
                         pRenderingInfo->renderArea.extent.height - 1;

   if (cmdbuf->state.gfx.render.bound_attachments) {
      fbinfo->width = att_width;
      fbinfo->height = att_height;
   } else {
      fbinfo->width = fbinfo->extent.maxx + 1;
      fbinfo->height = fbinfo->extent.maxy + 1;
   }

   assert(fbinfo->width && fbinfo->height);

   /* We need to re-emit the FS RSD when the color attachments change. */
   cmdbuf->state.gfx.fs.rsd = 0;
}

static void
preload_render_area_border(struct panvk_cmd_buffer *cmdbuf,
                           const VkRenderingInfo *render_info)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   bool render_area_is_32x32_aligned =
      ((fbinfo->extent.minx | fbinfo->extent.miny) % 32) == 0 &&
      (fbinfo->extent.maxx + 1 == fbinfo->width ||
       (fbinfo->extent.maxx % 32) == 31) &&
      (fbinfo->extent.maxy + 1 == fbinfo->height ||
       (fbinfo->extent.maxy % 32) == 31);

   /* If the render area is aligned on a 32x32 section, we're good. */
   if (render_area_is_32x32_aligned)
      return;

   /* We force preloading for all active attachments to preverse content falling
    * outside the render area, but we need to compensate with attachment clears
    * for attachments that were initially cleared.
    */
   uint32_t bound_atts = cmdbuf->state.gfx.render.bound_attachments;
   VkClearAttachment clear_atts[MAX_RTS + 2];
   uint32_t clear_att_count = 0;

   for (uint32_t i = 0; i < render_info->colorAttachmentCount; i++) {
      if (bound_atts & MESA_VK_RP_ATTACHMENT_COLOR_BIT(i)) {
         if (fbinfo->rts[i].clear) {
            const VkRenderingAttachmentInfo *att =
               &render_info->pColorAttachments[i];

            clear_atts[clear_att_count++] = (VkClearAttachment){
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .colorAttachment = i,
               .clearValue = att->clearValue,
            };
         }

         fbinfo->rts[i].preload = true;
         fbinfo->rts[i].clear = false;
      }
   }

   if (bound_atts & MESA_VK_RP_ATTACHMENT_DEPTH_BIT) {
      if (fbinfo->zs.clear.z) {
         const VkRenderingAttachmentInfo *att = render_info->pDepthAttachment;

         clear_atts[clear_att_count++] = (VkClearAttachment){
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .clearValue = att->clearValue,
         };
      }

      fbinfo->zs.preload.z = true;
      fbinfo->zs.clear.z = false;
   }

   if (bound_atts & MESA_VK_RP_ATTACHMENT_STENCIL_BIT) {
      if (fbinfo->zs.clear.s) {
         const VkRenderingAttachmentInfo *att = render_info->pStencilAttachment;

         clear_atts[clear_att_count++] = (VkClearAttachment){
            .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .clearValue = att->clearValue,
         };
      }

      fbinfo->zs.preload.s = true;
      fbinfo->zs.clear.s = false;
   }

   if (clear_att_count) {
      VkClearRect clear_rect = {
         .rect = render_info->renderArea,
         .baseArrayLayer = 0,
         .layerCount = render_info->layerCount,
      };

      panvk_per_arch(CmdClearAttachments)(panvk_cmd_buffer_to_handle(cmdbuf),
                                          clear_att_count, clear_atts, 1,
                                          &clear_rect);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginRendering)(VkCommandBuffer commandBuffer,
                                  const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   panvk_cmd_begin_rendering_init_state(cmdbuf, pRenderingInfo);

   bool resuming = cmdbuf->state.gfx.render.flags & VK_RENDERING_RESUMING_BIT;

   /* If we're not resuming, cur_batch should be NULL.
    * However, this currently isn't true because of how events are implemented.
    * XXX: Rewrite events to not close and open batch and add an assert here.
    */
   if (cmdbuf->cur_batch && !resuming)
      panvk_per_arch(cmd_close_batch)(cmdbuf);

   /* The opened batch might have been disrupted by a compute job.
    * We need to preload in that case. */
   if (resuming && !cmdbuf->cur_batch)
      panvk_per_arch(cmd_preload_fb_after_batch_split)(cmdbuf);

   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   if (!resuming)
      preload_render_area_border(cmdbuf, pRenderingInfo);
}

static void
resolve_attachments(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   bool needs_resolve = false;

   unsigned bound_atts = cmdbuf->state.gfx.render.bound_attachments;
   unsigned color_att_count =
      util_last_bit(bound_atts & MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS);
   VkRenderingAttachmentInfo color_atts[MAX_RTS];
   for (uint32_t i = 0; i < color_att_count; i++) {
      const struct panvk_resolve_attachment *resolve_info =
         &cmdbuf->state.gfx.render.color_attachments.resolve[i];
      struct panvk_image_view *src_iview =
         cmdbuf->state.gfx.render.color_attachments.iviews[i];

      color_atts[i] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = panvk_image_view_to_handle(src_iview),
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
         .resolveMode = resolve_info->mode,
         .resolveImageView =
            panvk_image_view_to_handle(resolve_info->dst_iview),
         .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
      };

      if (resolve_info->mode != VK_RESOLVE_MODE_NONE)
         needs_resolve = true;
   }

   const struct panvk_resolve_attachment *resolve_info =
      &cmdbuf->state.gfx.render.z_attachment.resolve;
   struct panvk_image_view *src_iview =
      cmdbuf->state.gfx.render.z_attachment.iview;
   VkRenderingAttachmentInfo z_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = panvk_image_view_to_handle(src_iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = resolve_info->mode,
      .resolveImageView = panvk_image_view_to_handle(resolve_info->dst_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   if (resolve_info->mode != VK_RESOLVE_MODE_NONE)
      needs_resolve = true;

   resolve_info = &cmdbuf->state.gfx.render.s_attachment.resolve;
   src_iview = cmdbuf->state.gfx.render.s_attachment.iview;

   VkRenderingAttachmentInfo s_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = panvk_image_view_to_handle(src_iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = resolve_info->mode,
      .resolveImageView = panvk_image_view_to_handle(resolve_info->dst_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   if (resolve_info->mode != VK_RESOLVE_MODE_NONE)
      needs_resolve = true;

   if (!needs_resolve)
      return;

   const VkRenderingInfo render_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {
         .offset.x = fbinfo->extent.minx,
         .offset.y = fbinfo->extent.miny,
         .extent.width = fbinfo->extent.maxx - fbinfo->extent.minx + 1,
         .extent.height = fbinfo->extent.maxy - fbinfo->extent.miny + 1,
      },
      .layerCount = cmdbuf->state.gfx.render.layer_count,
      .viewMask = 0,
      .colorAttachmentCount = color_att_count,
      .pColorAttachments = color_atts,
      .pDepthAttachment = &z_att,
      .pStencilAttachment = &s_att,
   };

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_resolve_rendering(&cmdbuf->vk, &dev->meta, &render_info);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndRendering)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (!(cmdbuf->state.gfx.render.flags & VK_RENDERING_SUSPENDING_BIT)) {
      struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
      bool clear = fbinfo->zs.clear.z | fbinfo->zs.clear.s;
      for (unsigned i = 0; i < fbinfo->rt_count; i++)
         clear |= fbinfo->rts[i].clear;

      if (clear)
         panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);

      panvk_per_arch(cmd_close_batch)(cmdbuf);
      cmdbuf->cur_batch = NULL;
      resolve_attachments(cmdbuf);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindVertexBuffers)(VkCommandBuffer commandBuffer,
                                     uint32_t firstBinding,
                                     uint32_t bindingCount,
                                     const VkBuffer *pBuffers,
                                     const VkDeviceSize *pOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   assert(firstBinding + bindingCount <= MAX_VBS);

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(panvk_buffer, buffer, pBuffers[i]);

      cmdbuf->state.gfx.vb.bufs[firstBinding + i].address =
         panvk_buffer_gpu_ptr(buffer, pOffsets[i]);
      cmdbuf->state.gfx.vb.bufs[firstBinding + i].size =
         panvk_buffer_range(buffer, pOffsets[i], VK_WHOLE_SIZE);
   }

   cmdbuf->state.gfx.vb.count =
      MAX2(cmdbuf->state.gfx.vb.count, firstBinding + bindingCount);
   cmdbuf->state.gfx.vs.attrib_bufs = 0;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindIndexBuffer)(VkCommandBuffer commandBuffer,
                                   VkBuffer buffer, VkDeviceSize offset,
                                   VkIndexType indexType)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buf, buffer);

   cmdbuf->state.gfx.ib.buffer = buf;
   cmdbuf->state.gfx.ib.offset = offset;
   cmdbuf->state.gfx.ib.index_size = vk_index_type_to_bytes(indexType);
}
