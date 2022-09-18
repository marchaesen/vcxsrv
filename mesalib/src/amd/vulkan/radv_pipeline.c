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

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/os_time.h"
#include "util/u_atomic.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"
#include "vk_pipeline.h"
#include "vk_util.h"

#include "util/debug.h"
#include "ac_binary.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "aco_interface.h"
#include "sid.h"
#include "vk_format.h"

struct radv_blend_state {
   uint32_t blend_enable_4bit;
   uint32_t need_src_alpha;

   uint32_t cb_target_mask;
   uint32_t cb_target_enabled_4bit;
   uint32_t sx_mrt_blend_opt[8];
   uint32_t cb_blend_control[8];

   uint32_t spi_shader_col_format;
   uint32_t col_format_is_int8;
   uint32_t col_format_is_int10;
   uint32_t col_format_is_float32;
   uint32_t cb_shader_mask;
   uint32_t db_alpha_to_mask;

   uint32_t commutative_4bit;

   bool mrt0_is_dual_src;
};

struct radv_depth_stencil_state {
   uint32_t db_render_control;
   uint32_t db_render_override;
   uint32_t db_render_override2;
};

struct radv_dsa_order_invariance {
   /* Whether the final result in Z/S buffers is guaranteed to be
    * invariant under changes to the order in which fragments arrive.
    */
   bool zs;

   /* Whether the set of fragments that pass the combined Z/S test is
    * guaranteed to be invariant under changes to the order in which
    * fragments arrive.
    */
   bool pass_set;
};

static bool
radv_is_raster_enabled(const struct radv_graphics_pipeline *pipeline,
                       const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   return !pCreateInfo->pRasterizationState->rasterizerDiscardEnable ||
          (pipeline->dynamic_states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE);
}

static bool
radv_is_static_vrs_enabled(const struct radv_graphics_pipeline *pipeline,
                           const struct vk_graphics_pipeline_state *state)
{
   if (!state->fsr)
      return false;

   return state->fsr->fragment_size.width != 1 || state->fsr->fragment_size.height != 1 ||
          state->fsr->combiner_ops[0] != VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR ||
          state->fsr->combiner_ops[1] != VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
}

static bool
radv_is_vrs_enabled(const struct radv_graphics_pipeline *pipeline,
                    const struct vk_graphics_pipeline_state *state)
{
   return radv_is_static_vrs_enabled(pipeline, state) ||
          (pipeline->dynamic_states & RADV_DYNAMIC_FRAGMENT_SHADING_RATE);
}

static bool
radv_pipeline_has_ds_attachments(const struct vk_render_pass_state *rp)
{
   return rp->depth_attachment_format != VK_FORMAT_UNDEFINED ||
          rp->stencil_attachment_format != VK_FORMAT_UNDEFINED;
}

static bool
radv_pipeline_has_color_attachments(const struct vk_render_pass_state *rp)
{
   for (uint32_t i = 0; i < rp->color_attachment_count; ++i) {
      if (rp->color_attachment_formats[i] != VK_FORMAT_UNDEFINED)
         return true;
   }

   return false;
}

static bool
radv_pipeline_has_ngg(const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *shader = pipeline->base.shaders[pipeline->last_vgt_api_stage];

   return shader->info.is_ngg;
}

bool
radv_pipeline_has_ngg_passthrough(const struct radv_graphics_pipeline *pipeline)
{
   assert(radv_pipeline_has_ngg(pipeline));

   struct radv_shader *shader = pipeline->base.shaders[pipeline->last_vgt_api_stage];

   return shader->info.is_ngg_passthrough;
}

bool
radv_pipeline_has_gs_copy_shader(const struct radv_pipeline *pipeline)
{
   return !!pipeline->gs_copy_shader;
}

static struct radv_pipeline_slab *
radv_pipeline_slab_create(struct radv_device *device, struct radv_pipeline *pipeline,
                          uint32_t code_size)
{
   struct radv_pipeline_slab *slab;

   slab = calloc(1, sizeof(*slab));
   if (!slab)
      return NULL;

   slab->ref_count = 1;

   slab->alloc = radv_alloc_shader_memory(device, code_size, pipeline);
   if (!slab->alloc) {
      free(slab);
      return NULL;
   }

   return slab;
}

void
radv_pipeline_slab_destroy(struct radv_device *device, struct radv_pipeline_slab *slab)
{
   if (!p_atomic_dec_zero(&slab->ref_count))
      return;

   radv_free_shader_memory(device, slab->alloc);
   free(slab);
}

void
radv_pipeline_destroy(struct radv_device *device, struct radv_pipeline *pipeline,
                      const VkAllocationCallbacks *allocator)
{
   if (pipeline->type == RADV_PIPELINE_GRAPHICS) {
      struct radv_graphics_pipeline *graphics_pipeline = radv_pipeline_to_graphics(pipeline);

      if (graphics_pipeline->ps_epilog)
         radv_shader_part_unref(device, graphics_pipeline->ps_epilog);

      vk_free(&device->vk.alloc, graphics_pipeline->state_data);
   } else if (pipeline->type == RADV_PIPELINE_COMPUTE) {
      struct radv_compute_pipeline *compute_pipeline = radv_pipeline_to_compute(pipeline);

      free(compute_pipeline->rt_group_handles);
      free(compute_pipeline->rt_stack_sizes);
   } else if (pipeline->type == RADV_PIPELINE_LIBRARY) {
      struct radv_library_pipeline *library_pipeline = radv_pipeline_to_library(pipeline);

      free(library_pipeline->groups);
      for (uint32_t i = 0; i < library_pipeline->stage_count; i++) {
         RADV_FROM_HANDLE(vk_shader_module, module, library_pipeline->stages[i].module);
         if (module) {
            vk_object_base_finish(&module->base);
            ralloc_free(module);
         }
      }
      free(library_pipeline->stages);
      free(library_pipeline->identifiers);
      free(library_pipeline->hashes);
   } else if (pipeline->type == RADV_PIPELINE_GRAPHICS_LIB) {
      struct radv_graphics_lib_pipeline *gfx_pipeline_lib =
         radv_pipeline_to_graphics_lib(pipeline);

      radv_pipeline_layout_finish(device, &gfx_pipeline_lib->layout);

      for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
         ralloc_free(pipeline->retained_shaders[i].nir);
      }

      if (gfx_pipeline_lib->base.ps_epilog)
         radv_shader_part_unref(device, gfx_pipeline_lib->base.ps_epilog);

      vk_free(&device->vk.alloc, gfx_pipeline_lib->base.state_data);
   }

   if (pipeline->slab)
      radv_pipeline_slab_destroy(device, pipeline->slab);

   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i)
      if (pipeline->shaders[i])
         radv_shader_unref(device, pipeline->shaders[i]);

   if (pipeline->gs_copy_shader)
      radv_shader_unref(device, pipeline->gs_copy_shader);

   if (pipeline->cs.buf)
      free(pipeline->cs.buf);

   vk_object_base_finish(&pipeline->base);
   vk_free2(&device->vk.alloc, allocator, pipeline);
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyPipeline(VkDevice _device, VkPipeline _pipeline,
                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   radv_pipeline_destroy(device, pipeline, pAllocator);
}

uint32_t
radv_get_hash_flags(const struct radv_device *device, bool stats)
{
   uint32_t hash_flags = 0;

   if (device->physical_device->use_ngg_culling)
      hash_flags |= RADV_HASH_SHADER_USE_NGG_CULLING;
   if (device->instance->perftest_flags & RADV_PERFTEST_EMULATE_RT)
      hash_flags |= RADV_HASH_SHADER_EMULATE_RT;
   if (device->physical_device->rt_wave_size == 64)
      hash_flags |= RADV_HASH_SHADER_RT_WAVE64;
   if (device->physical_device->cs_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_CS_WAVE32;
   if (device->physical_device->ps_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_PS_WAVE32;
   if (device->physical_device->ge_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_GE_WAVE32;
   if (device->physical_device->use_llvm)
      hash_flags |= RADV_HASH_SHADER_LLVM;
   if (stats)
      hash_flags |= RADV_HASH_SHADER_KEEP_STATISTICS;
   if (device->robust_buffer_access) /* forces per-attribute vertex descriptors */
      hash_flags |= RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS;
   if (device->robust_buffer_access2) /* affects load/store vectorizer */
      hash_flags |= RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS2;
   if (device->instance->debug_flags & RADV_DEBUG_SPLIT_FMA)
      hash_flags |= RADV_HASH_SHADER_SPLIT_FMA;
   return hash_flags;
}

static void
radv_pipeline_init_scratch(const struct radv_device *device, struct radv_pipeline *pipeline)
{
   unsigned scratch_bytes_per_wave = 0;
   unsigned max_waves = 0;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (pipeline->shaders[i] && pipeline->shaders[i]->config.scratch_bytes_per_wave) {
         unsigned max_stage_waves = device->scratch_waves;

         scratch_bytes_per_wave =
            MAX2(scratch_bytes_per_wave, pipeline->shaders[i]->config.scratch_bytes_per_wave);

         max_stage_waves =
            MIN2(max_stage_waves, 4 * device->physical_device->rad_info.num_cu *
                 radv_get_max_waves(device, pipeline->shaders[i], i));
         max_waves = MAX2(max_waves, max_stage_waves);
      }
   }

   pipeline->scratch_bytes_per_wave = scratch_bytes_per_wave;
   pipeline->max_waves = max_waves;
}

static uint32_t
si_translate_blend_function(VkBlendOp op)
{
   switch (op) {
   case VK_BLEND_OP_ADD:
      return V_028780_COMB_DST_PLUS_SRC;
   case VK_BLEND_OP_SUBTRACT:
      return V_028780_COMB_SRC_MINUS_DST;
   case VK_BLEND_OP_REVERSE_SUBTRACT:
      return V_028780_COMB_DST_MINUS_SRC;
   case VK_BLEND_OP_MIN:
      return V_028780_COMB_MIN_DST_SRC;
   case VK_BLEND_OP_MAX:
      return V_028780_COMB_MAX_DST_SRC;
   default:
      return 0;
   }
}

static uint32_t
si_translate_blend_factor(enum amd_gfx_level gfx_level, VkBlendFactor factor)
{
   switch (factor) {
   case VK_BLEND_FACTOR_ZERO:
      return V_028780_BLEND_ZERO;
   case VK_BLEND_FACTOR_ONE:
      return V_028780_BLEND_ONE;
   case VK_BLEND_FACTOR_SRC_COLOR:
      return V_028780_BLEND_SRC_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return V_028780_BLEND_ONE_MINUS_SRC_COLOR;
   case VK_BLEND_FACTOR_DST_COLOR:
      return V_028780_BLEND_DST_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
      return V_028780_BLEND_ONE_MINUS_DST_COLOR;
   case VK_BLEND_FACTOR_SRC_ALPHA:
      return V_028780_BLEND_SRC_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
      return V_028780_BLEND_ONE_MINUS_SRC_ALPHA;
   case VK_BLEND_FACTOR_DST_ALPHA:
      return V_028780_BLEND_DST_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
      return V_028780_BLEND_ONE_MINUS_DST_ALPHA;
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_CONSTANT_COLOR_GFX11
                                : V_028780_BLEND_CONSTANT_COLOR_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX11
                                 : V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX6;
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_CONSTANT_ALPHA_GFX11
                                 : V_028780_BLEND_CONSTANT_ALPHA_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX11
                                 : V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX6;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
      return V_028780_BLEND_SRC_ALPHA_SATURATE;
   case VK_BLEND_FACTOR_SRC1_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_SRC1_COLOR_GFX11 : V_028780_BLEND_SRC1_COLOR_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_INV_SRC1_COLOR_GFX11
                                 : V_028780_BLEND_INV_SRC1_COLOR_GFX6;
   case VK_BLEND_FACTOR_SRC1_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_SRC1_ALPHA_GFX11 : V_028780_BLEND_SRC1_ALPHA_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_INV_SRC1_ALPHA_GFX11
                                 : V_028780_BLEND_INV_SRC1_ALPHA_GFX6;
   default:
      return 0;
   }
}

static uint32_t
si_translate_blend_opt_function(VkBlendOp op)
{
   switch (op) {
   case VK_BLEND_OP_ADD:
      return V_028760_OPT_COMB_ADD;
   case VK_BLEND_OP_SUBTRACT:
      return V_028760_OPT_COMB_SUBTRACT;
   case VK_BLEND_OP_REVERSE_SUBTRACT:
      return V_028760_OPT_COMB_REVSUBTRACT;
   case VK_BLEND_OP_MIN:
      return V_028760_OPT_COMB_MIN;
   case VK_BLEND_OP_MAX:
      return V_028760_OPT_COMB_MAX;
   default:
      return V_028760_OPT_COMB_BLEND_DISABLED;
   }
}

static uint32_t
si_translate_blend_opt_factor(VkBlendFactor factor, bool is_alpha)
{
   switch (factor) {
   case VK_BLEND_FACTOR_ZERO:
      return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_ALL;
   case VK_BLEND_FACTOR_ONE:
      return V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE;
   case VK_BLEND_FACTOR_SRC_COLOR:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0
                      : V_028760_BLEND_OPT_PRESERVE_C1_IGNORE_C0;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1
                      : V_028760_BLEND_OPT_PRESERVE_C0_IGNORE_C1;
   case VK_BLEND_FACTOR_SRC_ALPHA:
      return V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
      return V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE
                      : V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;
   default:
      return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
   }
}

/**
 * Get rid of DST in the blend factors by commuting the operands:
 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
 */
static void
si_blend_remove_dst(VkBlendOp *func, VkBlendFactor *src_factor, VkBlendFactor *dst_factor,
                    VkBlendFactor expected_dst, VkBlendFactor replacement_src)
{
   if (*src_factor == expected_dst && *dst_factor == VK_BLEND_FACTOR_ZERO) {
      *src_factor = VK_BLEND_FACTOR_ZERO;
      *dst_factor = replacement_src;

      /* Commuting the operands requires reversing subtractions. */
      if (*func == VK_BLEND_OP_SUBTRACT)
         *func = VK_BLEND_OP_REVERSE_SUBTRACT;
      else if (*func == VK_BLEND_OP_REVERSE_SUBTRACT)
         *func = VK_BLEND_OP_SUBTRACT;
   }
}

static bool
si_blend_factor_uses_dst(VkBlendFactor factor)
{
   return factor == VK_BLEND_FACTOR_DST_COLOR || factor == VK_BLEND_FACTOR_DST_ALPHA ||
          factor == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
          factor == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA ||
          factor == VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
}

static bool
is_dual_src(VkBlendFactor factor)
{
   switch (factor) {
   case VK_BLEND_FACTOR_SRC1_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
   case VK_BLEND_FACTOR_SRC1_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return true;
   default:
      return false;
   }
}

static unsigned
radv_choose_spi_color_format(const struct radv_device *device, VkFormat vk_format,
                             bool blend_enable, bool blend_need_alpha)
{
   const struct util_format_description *desc = vk_format_description(vk_format);
   bool use_rbplus = device->physical_device->rad_info.rbplus_allowed;
   struct ac_spi_color_formats formats = {0};
   unsigned format, ntype, swap;

   format = radv_translate_colorformat(vk_format);
   ntype = radv_translate_color_numformat(vk_format, desc,
                                          vk_format_get_first_non_void_channel(vk_format));
   swap = radv_translate_colorswap(vk_format, false);

   ac_choose_spi_color_formats(format, swap, ntype, false, use_rbplus, &formats);

   if (blend_enable && blend_need_alpha)
      return formats.blend_alpha;
   else if (blend_need_alpha)
      return formats.alpha;
   else if (blend_enable)
      return formats.blend;
   else
      return formats.normal;
}

static bool
format_is_int8(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);
   int channel = vk_format_get_first_non_void_channel(format);

   return channel >= 0 && desc->channel[channel].pure_integer && desc->channel[channel].size == 8;
}

static bool
format_is_int10(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   if (desc->nr_channels != 4)
      return false;
   for (unsigned i = 0; i < 4; i++) {
      if (desc->channel[i].pure_integer && desc->channel[i].size == 10)
         return true;
   }
   return false;
}

static bool
format_is_float32(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);
   int channel = vk_format_get_first_non_void_channel(format);

   return channel >= 0 &&
          desc->channel[channel].type == UTIL_FORMAT_TYPE_FLOAT && desc->channel[channel].size == 32;
}

static unsigned
radv_compact_spi_shader_col_format(const struct radv_shader *ps,
                                   const struct radv_blend_state *blend)
{
   unsigned spi_shader_col_format = blend->spi_shader_col_format;
   unsigned value = 0, num_mrts = 0;
   unsigned i, num_targets;

   /* Make sure to clear color attachments without exports because MRT holes are removed during
    * compilation for optimal performance.
    */
   spi_shader_col_format &= ps->info.ps.colors_written;

   /* Compute the number of MRTs. */
   num_targets = DIV_ROUND_UP(util_last_bit(spi_shader_col_format), 4);

   /* Remove holes in spi_shader_col_format. */
   for (i = 0; i < num_targets; i++) {
      unsigned spi_format = (spi_shader_col_format >> (i * 4)) & 0xf;

      if (spi_format) {
         value |= spi_format << (num_mrts * 4);
         num_mrts++;
      }
   }

   return value;
}

static void
radv_pipeline_compute_spi_color_formats(const struct radv_graphics_pipeline *pipeline,
                                        struct radv_blend_state *blend,
                                        const struct vk_graphics_pipeline_state *state)
{
   unsigned col_format = 0, is_int8 = 0, is_int10 = 0, is_float32 = 0;

   for (unsigned i = 0; i < state->rp->color_attachment_count; ++i) {
      unsigned cf;
      VkFormat fmt = state->rp->color_attachment_formats[i];

      if (fmt == VK_FORMAT_UNDEFINED || !(blend->cb_target_mask & (0xfu << (i * 4)))) {
         cf = V_028714_SPI_SHADER_ZERO;
      } else {
         bool blend_enable = blend->blend_enable_4bit & (0xfu << (i * 4));

         cf = radv_choose_spi_color_format(pipeline->base.device, fmt, blend_enable,
                                           blend->need_src_alpha & (1 << i));

         if (format_is_int8(fmt))
            is_int8 |= 1 << i;
         if (format_is_int10(fmt))
            is_int10 |= 1 << i;
         if (format_is_float32(fmt))
            is_float32 |= 1 << i;
      }

      col_format |= cf << (4 * i);
   }

   if (!(col_format & 0xf) && blend->need_src_alpha & (1 << 0)) {
      /* When a subpass doesn't have any color attachments, write the
       * alpha channel of MRT0 when alpha coverage is enabled because
       * the depth attachment needs it.
       */
      col_format |= V_028714_SPI_SHADER_32_AR;
   }

   /* The output for dual source blending should have the same format as
    * the first output.
    */
   if (blend->mrt0_is_dual_src) {
      assert(!(col_format >> 4));
      col_format |= (col_format & 0xf) << 4;
   }

   blend->cb_shader_mask = ac_get_cb_shader_mask(col_format);
   blend->spi_shader_col_format = col_format;
   blend->col_format_is_int8 = is_int8;
   blend->col_format_is_int10 = is_int10;
   blend->col_format_is_float32 = is_float32;
}

/*
 * Ordered so that for each i,
 * radv_format_meta_fs_key(radv_fs_key_format_exemplars[i]) == i.
 */
const VkFormat radv_fs_key_format_exemplars[NUM_META_FS_KEYS] = {
   VK_FORMAT_R32_SFLOAT,
   VK_FORMAT_R32G32_SFLOAT,
   VK_FORMAT_R8G8B8A8_UNORM,
   VK_FORMAT_R16G16B16A16_UNORM,
   VK_FORMAT_R16G16B16A16_SNORM,
   VK_FORMAT_R16G16B16A16_UINT,
   VK_FORMAT_R16G16B16A16_SINT,
   VK_FORMAT_R32G32B32A32_SFLOAT,
   VK_FORMAT_R8G8B8A8_UINT,
   VK_FORMAT_R8G8B8A8_SINT,
   VK_FORMAT_A2R10G10B10_UINT_PACK32,
   VK_FORMAT_A2R10G10B10_SINT_PACK32,
};

unsigned
radv_format_meta_fs_key(struct radv_device *device, VkFormat format)
{
   unsigned col_format = radv_choose_spi_color_format(device, format, false, false);
   assert(col_format != V_028714_SPI_SHADER_32_AR);

   bool is_int8 = format_is_int8(format);
   bool is_int10 = format_is_int10(format);

   if (col_format == V_028714_SPI_SHADER_UINT16_ABGR && is_int8)
      return 8;
   else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR && is_int8)
      return 9;
   else if (col_format == V_028714_SPI_SHADER_UINT16_ABGR && is_int10)
      return 10;
   else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR && is_int10)
      return 11;
   else {
      if (col_format >= V_028714_SPI_SHADER_32_AR)
         --col_format; /* Skip V_028714_SPI_SHADER_32_AR  since there is no such VkFormat */

      --col_format; /* Skip V_028714_SPI_SHADER_ZERO */
      return col_format;
   }
}

static void
radv_blend_check_commutativity(struct radv_blend_state *blend, VkBlendOp op, VkBlendFactor src,
                               VkBlendFactor dst, unsigned chanmask)
{
   /* Src factor is allowed when it does not depend on Dst. */
   static const uint32_t src_allowed =
      (1u << VK_BLEND_FACTOR_ONE) | (1u << VK_BLEND_FACTOR_SRC_COLOR) |
      (1u << VK_BLEND_FACTOR_SRC_ALPHA) | (1u << VK_BLEND_FACTOR_SRC_ALPHA_SATURATE) |
      (1u << VK_BLEND_FACTOR_CONSTANT_COLOR) | (1u << VK_BLEND_FACTOR_CONSTANT_ALPHA) |
      (1u << VK_BLEND_FACTOR_SRC1_COLOR) | (1u << VK_BLEND_FACTOR_SRC1_ALPHA) |
      (1u << VK_BLEND_FACTOR_ZERO) | (1u << VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR) |
      (1u << VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) |
      (1u << VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR) |
      (1u << VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA) |
      (1u << VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR) | (1u << VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA);

   if (dst == VK_BLEND_FACTOR_ONE && (src_allowed & (1u << src))) {
      /* Addition is commutative, but floating point addition isn't
       * associative: subtle changes can be introduced via different
       * rounding. Be conservative, only enable for min and max.
       */
      if (op == VK_BLEND_OP_MAX || op == VK_BLEND_OP_MIN)
         blend->commutative_4bit |= chanmask;
   }
}

static struct radv_blend_state
radv_pipeline_init_blend_state(struct radv_graphics_pipeline *pipeline,
                               const struct vk_graphics_pipeline_state *state)
{
   const struct radv_device *device = pipeline->base.device;
   struct radv_blend_state blend = {0};
   unsigned cb_color_control = 0;
   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   int i;

   if (device->instance->debug_flags & RADV_DEBUG_NO_ATOC_DITHERING)
   {
      blend.db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_OFFSET0(2) | S_028B70_ALPHA_TO_MASK_OFFSET1(2) |
                               S_028B70_ALPHA_TO_MASK_OFFSET2(2) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                               S_028B70_OFFSET_ROUND(0);
   }
   else
   {
      blend.db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_OFFSET0(3) | S_028B70_ALPHA_TO_MASK_OFFSET1(1) |
                               S_028B70_ALPHA_TO_MASK_OFFSET2(0) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                               S_028B70_OFFSET_ROUND(1);
   }

   if (state->ms && state->ms->alpha_to_coverage_enable) {
      blend.db_alpha_to_mask |= S_028B70_ALPHA_TO_MASK_ENABLE(1);
      blend.need_src_alpha |= 0x1;
   }

   blend.cb_target_mask = 0;
   if (state->cb) {
      for (i = 0; i < state->cb->attachment_count; i++) {
         unsigned blend_cntl = 0;
         unsigned srcRGB_opt, dstRGB_opt, srcA_opt, dstA_opt;
         VkBlendOp eqRGB = state->cb->attachments[i].color_blend_op;
         VkBlendFactor srcRGB = state->cb->attachments[i].src_color_blend_factor;
         VkBlendFactor dstRGB = state->cb->attachments[i].dst_color_blend_factor;
         VkBlendOp eqA = state->cb->attachments[i].alpha_blend_op;
         VkBlendFactor srcA = state->cb->attachments[i].src_alpha_blend_factor;
         VkBlendFactor dstA = state->cb->attachments[i].dst_alpha_blend_factor;

         blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) |
                                     S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);

         if (!state->cb->attachments[i].write_mask)
            continue;

         /* Ignore other blend targets if dual-source blending
          * is enabled to prevent wrong behaviour.
          */
         if (blend.mrt0_is_dual_src)
            continue;

         blend.cb_target_mask |= (unsigned)state->cb->attachments[i].write_mask << (4 * i);
         blend.cb_target_enabled_4bit |= 0xfu << (4 * i);
         if (!state->cb->attachments[i].blend_enable) {
            blend.cb_blend_control[i] = blend_cntl;
            continue;
         }

         if (is_dual_src(srcRGB) || is_dual_src(dstRGB) || is_dual_src(srcA) || is_dual_src(dstA))
            if (i == 0)
               blend.mrt0_is_dual_src = true;

         if (eqRGB == VK_BLEND_OP_MIN || eqRGB == VK_BLEND_OP_MAX) {
            srcRGB = VK_BLEND_FACTOR_ONE;
            dstRGB = VK_BLEND_FACTOR_ONE;
         }
         if (eqA == VK_BLEND_OP_MIN || eqA == VK_BLEND_OP_MAX) {
            srcA = VK_BLEND_FACTOR_ONE;
            dstA = VK_BLEND_FACTOR_ONE;
         }

         radv_blend_check_commutativity(&blend, eqRGB, srcRGB, dstRGB, 0x7u << (4 * i));
         radv_blend_check_commutativity(&blend, eqA, srcA, dstA, 0x8u << (4 * i));

         /* Blending optimizations for RB+.
          * These transformations don't change the behavior.
          *
          * First, get rid of DST in the blend factors:
          *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
          */
         si_blend_remove_dst(&eqRGB, &srcRGB, &dstRGB, VK_BLEND_FACTOR_DST_COLOR,
                             VK_BLEND_FACTOR_SRC_COLOR);

         si_blend_remove_dst(&eqA, &srcA, &dstA, VK_BLEND_FACTOR_DST_COLOR,
                             VK_BLEND_FACTOR_SRC_COLOR);

         si_blend_remove_dst(&eqA, &srcA, &dstA, VK_BLEND_FACTOR_DST_ALPHA,
                             VK_BLEND_FACTOR_SRC_ALPHA);

         /* Look up the ideal settings from tables. */
         srcRGB_opt = si_translate_blend_opt_factor(srcRGB, false);
         dstRGB_opt = si_translate_blend_opt_factor(dstRGB, false);
         srcA_opt = si_translate_blend_opt_factor(srcA, true);
         dstA_opt = si_translate_blend_opt_factor(dstA, true);

         /* Handle interdependencies. */
         if (si_blend_factor_uses_dst(srcRGB))
            dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
         if (si_blend_factor_uses_dst(srcA))
            dstA_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;

         if (srcRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE &&
             (dstRGB == VK_BLEND_FACTOR_ZERO || dstRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
              dstRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE))
            dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;

         /* Set the final value. */
         blend.sx_mrt_blend_opt[i] =
            S_028760_COLOR_SRC_OPT(srcRGB_opt) | S_028760_COLOR_DST_OPT(dstRGB_opt) |
            S_028760_COLOR_COMB_FCN(si_translate_blend_opt_function(eqRGB)) |
            S_028760_ALPHA_SRC_OPT(srcA_opt) | S_028760_ALPHA_DST_OPT(dstA_opt) |
            S_028760_ALPHA_COMB_FCN(si_translate_blend_opt_function(eqA));
         blend_cntl |= S_028780_ENABLE(1);

         blend_cntl |= S_028780_COLOR_COMB_FCN(si_translate_blend_function(eqRGB));
         blend_cntl |= S_028780_COLOR_SRCBLEND(si_translate_blend_factor(gfx_level, srcRGB));
         blend_cntl |= S_028780_COLOR_DESTBLEND(si_translate_blend_factor(gfx_level, dstRGB));
         if (srcA != srcRGB || dstA != dstRGB || eqA != eqRGB) {
            blend_cntl |= S_028780_SEPARATE_ALPHA_BLEND(1);
            blend_cntl |= S_028780_ALPHA_COMB_FCN(si_translate_blend_function(eqA));
            blend_cntl |= S_028780_ALPHA_SRCBLEND(si_translate_blend_factor(gfx_level, srcA));
            blend_cntl |= S_028780_ALPHA_DESTBLEND(si_translate_blend_factor(gfx_level, dstA));
         }
         blend.cb_blend_control[i] = blend_cntl;

         blend.blend_enable_4bit |= 0xfu << (i * 4);

         if (srcRGB == VK_BLEND_FACTOR_SRC_ALPHA || dstRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
             srcRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
             dstRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
             srcRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ||
             dstRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
            blend.need_src_alpha |= 1 << i;
      }
      for (i = state->cb->attachment_count; i < 8; i++) {
         blend.cb_blend_control[i] = 0;
         blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) |
                                     S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);
      }
   }

   if (device->physical_device->rad_info.has_rbplus) {
      /* Disable RB+ blend optimizations for dual source blending. */
      if (blend.mrt0_is_dual_src) {
         for (i = 0; i < 8; i++) {
            blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_NONE) |
                                        S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_NONE);
         }
      }

      /* RB+ doesn't work with dual source blending, logic op and
       * RESOLVE.
       */
      if (blend.mrt0_is_dual_src || (state->cb && state->cb->logic_op_enable) ||
          (device->physical_device->rad_info.gfx_level >= GFX11 && blend.blend_enable_4bit))
         cb_color_control |= S_028808_DISABLE_DUAL_QUAD(1);
   }

   if (blend.cb_target_mask)
      cb_color_control |= S_028808_MODE(V_028808_CB_NORMAL);
   else
      cb_color_control |= S_028808_MODE(V_028808_CB_DISABLE);

   radv_pipeline_compute_spi_color_formats(pipeline, &blend, state);

   pipeline->cb_color_control = cb_color_control;

   return blend;
}

static uint32_t
si_translate_fill(VkPolygonMode func)
{
   switch (func) {
   case VK_POLYGON_MODE_FILL:
      return V_028814_X_DRAW_TRIANGLES;
   case VK_POLYGON_MODE_LINE:
      return V_028814_X_DRAW_LINES;
   case VK_POLYGON_MODE_POINT:
      return V_028814_X_DRAW_POINTS;
   default:
      assert(0);
      return V_028814_X_DRAW_POINTS;
   }
}

static unsigned
radv_pipeline_color_samples(const struct vk_graphics_pipeline_state *state)
{
   if (radv_pipeline_has_color_attachments(state->rp)) {
      unsigned color_attachment_samples = 0;
      for (uint32_t i = 0; i < state->rp->color_attachment_count; i++) {
         if (state->rp->color_attachment_formats[i] != VK_FORMAT_UNDEFINED) {
            color_attachment_samples =
               MAX2(color_attachment_samples, state->rp->color_attachment_samples[i]);
         }
      }

      if (color_attachment_samples)
         return color_attachment_samples;
   }

   return state->ms ? state->ms->rasterization_samples : 1;
}

static unsigned
radv_pipeline_depth_samples(const struct vk_graphics_pipeline_state *state)
{
   if (state->rp->depth_stencil_attachment_samples && radv_pipeline_has_ds_attachments(state->rp)) {
      return state->rp->depth_stencil_attachment_samples;
   }

   return state->ms ? state->ms->rasterization_samples : 1;
}

static uint8_t
radv_pipeline_get_ps_iter_samples(const struct vk_graphics_pipeline_state *state)
{
   uint32_t ps_iter_samples = 1;
   uint32_t num_samples = radv_pipeline_color_samples(state);

   if (state->ms && state->ms->sample_shading_enable) {
      ps_iter_samples = ceilf(state->ms->min_sample_shading * num_samples);
      ps_iter_samples = util_next_power_of_two(ps_iter_samples);
   }
   return ps_iter_samples;
}

static bool
radv_is_depth_write_enabled(const struct vk_depth_stencil_state *ds)
{
   return ds->depth.test_enable && ds->depth.write_enable &&
          ds->depth.compare_op != VK_COMPARE_OP_NEVER;
}

static bool
radv_writes_stencil(const struct vk_stencil_test_face_state *face)
{
   return face->write_mask &&
          (face->op.fail != VK_STENCIL_OP_KEEP || face->op.pass != VK_STENCIL_OP_KEEP ||
           face->op.depth_fail != VK_STENCIL_OP_KEEP);
}

static bool
radv_is_stencil_write_enabled(const struct vk_depth_stencil_state *ds)
{
   return ds->stencil.test_enable &&
          (radv_writes_stencil(&ds->stencil.front) || radv_writes_stencil(&ds->stencil.back));
}

static bool
radv_order_invariant_stencil_op(VkStencilOp op)
{
   /* REPLACE is normally order invariant, except when the stencil
    * reference value is written by the fragment shader. Tracking this
    * interaction does not seem worth the effort, so be conservative.
    */
   return op != VK_STENCIL_OP_INCREMENT_AND_CLAMP && op != VK_STENCIL_OP_DECREMENT_AND_CLAMP &&
          op != VK_STENCIL_OP_REPLACE;
}

static bool
radv_order_invariant_stencil_state(const struct vk_stencil_test_face_state *face)
{
   /* Compute whether, assuming Z writes are disabled, this stencil state
    * is order invariant in the sense that the set of passing fragments as
    * well as the final stencil buffer result does not depend on the order
    * of fragments.
    */
   return !face->write_mask ||
          /* The following assumes that Z writes are disabled. */
          (face->op.compare == VK_COMPARE_OP_ALWAYS &&
           radv_order_invariant_stencil_op(face->op.pass) &&
           radv_order_invariant_stencil_op(face->op.depth_fail)) ||
          (face->op.compare == VK_COMPARE_OP_NEVER &&
           radv_order_invariant_stencil_op(face->op.fail));
}

static bool
radv_pipeline_has_dynamic_ds_states(const struct radv_graphics_pipeline *pipeline)
{
   return !!(pipeline->dynamic_states & (RADV_DYNAMIC_DEPTH_TEST_ENABLE |
                                         RADV_DYNAMIC_DEPTH_WRITE_ENABLE |
                                         RADV_DYNAMIC_DEPTH_COMPARE_OP |
                                         RADV_DYNAMIC_STENCIL_TEST_ENABLE |
                                         RADV_DYNAMIC_STENCIL_WRITE_MASK |
                                         RADV_DYNAMIC_STENCIL_OP));
}

static bool
radv_pipeline_out_of_order_rast(struct radv_graphics_pipeline *pipeline,
                                const struct radv_blend_state *blend,
                                const struct vk_graphics_pipeline_state *state)
{
   unsigned colormask = blend->cb_target_enabled_4bit;

   if (!pipeline->base.device->physical_device->out_of_order_rast_allowed)
      return false;

   /* Be conservative if a logic operation is enabled with color buffers. */
   if (colormask && state->cb && state->cb->logic_op_enable)
      return false;

   /* Be conservative if an extended dynamic depth/stencil state is
    * enabled because the driver can't update out-of-order rasterization
    * dynamically.
    */
   if (radv_pipeline_has_dynamic_ds_states(pipeline))
      return false;

   /* Default depth/stencil invariance when no attachment is bound. */
   struct radv_dsa_order_invariance dsa_order_invariant = {.zs = true, .pass_set = true};

   if (state->ds) {
      bool has_stencil = state->rp->stencil_attachment_format != VK_FORMAT_UNDEFINED;
      struct radv_dsa_order_invariance order_invariance[2];
      struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];

      /* Compute depth/stencil order invariance in order to know if
       * it's safe to enable out-of-order.
       */
      bool zfunc_is_ordered = state->ds->depth.compare_op == VK_COMPARE_OP_NEVER ||
                              state->ds->depth.compare_op == VK_COMPARE_OP_LESS ||
                              state->ds->depth.compare_op == VK_COMPARE_OP_LESS_OR_EQUAL ||
                              state->ds->depth.compare_op == VK_COMPARE_OP_GREATER ||
                              state->ds->depth.compare_op == VK_COMPARE_OP_GREATER_OR_EQUAL;
      bool depth_write_enabled = radv_is_depth_write_enabled(state->ds);
      bool stencil_write_enabled = radv_is_stencil_write_enabled(state->ds);
      bool ds_write_enabled = depth_write_enabled || stencil_write_enabled;

      bool nozwrite_and_order_invariant_stencil =
         !ds_write_enabled ||
         (!depth_write_enabled && radv_order_invariant_stencil_state(&state->ds->stencil.front) &&
          radv_order_invariant_stencil_state(&state->ds->stencil.back));

      order_invariance[1].zs = nozwrite_and_order_invariant_stencil ||
                               (!stencil_write_enabled && zfunc_is_ordered);
      order_invariance[0].zs = !depth_write_enabled || zfunc_is_ordered;

      order_invariance[1].pass_set =
         nozwrite_and_order_invariant_stencil ||
         (!stencil_write_enabled &&
          (state->ds->depth.compare_op == VK_COMPARE_OP_ALWAYS ||
           state->ds->depth.compare_op == VK_COMPARE_OP_NEVER));
      order_invariance[0].pass_set =
         !depth_write_enabled ||
         (state->ds->depth.compare_op == VK_COMPARE_OP_ALWAYS ||
          state->ds->depth.compare_op == VK_COMPARE_OP_NEVER);

      dsa_order_invariant = order_invariance[has_stencil];
      if (!dsa_order_invariant.zs)
         return false;

      /* The set of PS invocations is always order invariant,
       * except when early Z/S tests are requested.
       */
      if (ps && ps->info.ps.writes_memory && ps->info.ps.early_fragment_test &&
          !dsa_order_invariant.pass_set)
         return false;

      /* Determine if out-of-order rasterization should be disabled when occlusion queries are used. */
      pipeline->disable_out_of_order_rast_for_occlusion = !dsa_order_invariant.pass_set;
   }

   /* No color buffers are enabled for writing. */
   if (!colormask)
      return true;

   unsigned blendmask = colormask & blend->blend_enable_4bit;

   if (blendmask) {
      /* Only commutative blending. */
      if (blendmask & ~blend->commutative_4bit)
         return false;

      if (!dsa_order_invariant.pass_set)
         return false;
   }

   if (colormask & ~blendmask)
      return false;

   return true;
}

static void
radv_pipeline_init_multisample_state(struct radv_graphics_pipeline *pipeline,
                                     const struct radv_blend_state *blend,
                                     const struct vk_graphics_pipeline_state *state,
                                     unsigned rast_prim)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_multisample_state *ms = &pipeline->ms;
   unsigned num_tile_pipes = pdevice->rad_info.num_tile_pipes;
   const VkConservativeRasterizationModeEXT mode = state->rs->conservative_mode;
   bool out_of_order_rast = false;
   uint32_t sample_mask = 0xffff;
   int ps_iter_samples = 1;

   ms->num_samples = state->ms ? state->ms->rasterization_samples : 1;

   /* From the Vulkan 1.1.129 spec, 26.7. Sample Shading:
    *
    * "Sample shading is enabled for a graphics pipeline:
    *
    * - If the interface of the fragment shader entry point of the
    *   graphics pipeline includes an input variable decorated
    *   with SampleId or SamplePosition. In this case
    *   minSampleShadingFactor takes the value 1.0.
    * - Else if the sampleShadingEnable member of the
    *   VkPipelineMultisampleStateCreateInfo structure specified
    *   when creating the graphics pipeline is set to VK_TRUE. In
    *   this case minSampleShadingFactor takes the value of
    *   VkPipelineMultisampleStateCreateInfo::minSampleShading.
    *
    * Otherwise, sample shading is considered disabled."
    */
   if (pipeline->base.shaders[MESA_SHADER_FRAGMENT]->info.ps.uses_sample_shading) {
      ps_iter_samples = ms->num_samples;
   } else {
      ps_iter_samples = radv_pipeline_get_ps_iter_samples(state);
   }

   if (state->rs->rasterization_order_amd == VK_RASTERIZATION_ORDER_RELAXED_AMD) {
      /* Out-of-order rasterization is explicitly enabled by the
       * application.
       */
      out_of_order_rast = true;
   } else {
      /* Determine if the driver can enable out-of-order
       * rasterization internally.
       */
      out_of_order_rast = radv_pipeline_out_of_order_rast(pipeline, blend, state);
   }

   ms->pa_sc_aa_config = 0;
   ms->db_eqaa = S_028804_HIGH_QUALITY_INTERSECTIONS(1) | S_028804_INCOHERENT_EQAA_READS(1) |
                 S_028804_INTERPOLATE_COMP_Z(1) | S_028804_STATIC_ANCHOR_ASSOCIATIONS(1);

   /* Adjust MSAA state if conservative rasterization is enabled. */
   if (mode != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT) {
      ms->pa_sc_aa_config |= S_028BE0_AA_MASK_CENTROID_DTMN(1);

      ms->db_eqaa |=
         S_028804_ENABLE_POSTZ_OVERRASTERIZATION(1) | S_028804_OVERRASTERIZATION_AMOUNT(4);
   }

   ms->pa_sc_mode_cntl_1 =
      S_028A4C_WALK_FENCE_ENABLE(1) | // TODO linear dst fixes
      S_028A4C_WALK_FENCE_SIZE(num_tile_pipes == 2 ? 2 : 3) |
      S_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(out_of_order_rast) |
      S_028A4C_OUT_OF_ORDER_WATER_MARK(0x7) |
      /* always 1: */
      S_028A4C_WALK_ALIGN8_PRIM_FITS_ST(1) | S_028A4C_SUPERTILE_WALK_ORDER_ENABLE(1) |
      S_028A4C_TILE_WALK_ORDER_ENABLE(1) | S_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(1) |
      S_028A4C_FORCE_EOV_CNTDWN_ENABLE(1) | S_028A4C_FORCE_EOV_REZ_ENABLE(1);
   ms->pa_sc_mode_cntl_0 = S_028A48_ALTERNATE_RBS_PER_TILE(pdevice->rad_info.gfx_level >= GFX9) |
                           S_028A48_VPORT_SCISSOR_ENABLE(1) |
                           S_028A48_LINE_STIPPLE_ENABLE(state->rs->line.stipple.enable);

   if (state->rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT &&
       radv_rast_prim_is_line(rast_prim)) {
      /* From the Vulkan spec 1.3.221:
       *
       * "When Bresenham lines are being rasterized, sample locations may all be treated as being at
       * the pixel center (this may affect attribute and depth interpolation)."
       *
       * "One consequence of this is that Bresenham lines cover the same pixels regardless of the
       * number of rasterization samples, and cover all samples in those pixels (unless masked out
       * or killed)."
       */
      ms->num_samples = 1;
   }

   if (ms->num_samples > 1) {
      uint32_t z_samples = radv_pipeline_depth_samples(state);
      unsigned log_samples = util_logbase2(ms->num_samples);
      unsigned log_z_samples = util_logbase2(z_samples);
      unsigned log_ps_iter_samples = util_logbase2(ps_iter_samples);
      ms->pa_sc_mode_cntl_0 |= S_028A48_MSAA_ENABLE(1);
      ms->db_eqaa |= S_028804_MAX_ANCHOR_SAMPLES(log_z_samples) |
                     S_028804_PS_ITER_SAMPLES(log_ps_iter_samples) |
                     S_028804_MASK_EXPORT_NUM_SAMPLES(log_samples) |
                     S_028804_ALPHA_TO_MASK_NUM_SAMPLES(log_samples);
      ms->pa_sc_aa_config |=
         S_028BE0_MSAA_NUM_SAMPLES(log_samples) |
         S_028BE0_MAX_SAMPLE_DIST(radv_get_default_max_sample_dist(log_samples)) |
         S_028BE0_MSAA_EXPOSED_SAMPLES(log_samples) | /* CM_R_028BE0_PA_SC_AA_CONFIG */
         S_028BE0_COVERED_CENTROID_IS_CENTER(pdevice->rad_info.gfx_level >= GFX10_3);
      ms->pa_sc_mode_cntl_1 |= S_028A4C_PS_ITER_SAMPLE(ps_iter_samples > 1);
      if (ps_iter_samples > 1)
         pipeline->spi_baryc_cntl |= S_0286E0_POS_FLOAT_LOCATION(2);
   }

   if (state->ms) {
      sample_mask = state->ms->sample_mask & 0xffff;
   }

   ms->pa_sc_aa_mask[0] = sample_mask | ((uint32_t)sample_mask << 16);
   ms->pa_sc_aa_mask[1] = sample_mask | ((uint32_t)sample_mask << 16);
}

static void
gfx103_pipeline_init_vrs_state(struct radv_graphics_pipeline *pipeline,
                               const struct vk_graphics_pipeline_state *state)
{
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   struct radv_multisample_state *ms = &pipeline->ms;
   struct radv_vrs_state *vrs = &pipeline->vrs;

   if ((state->ms && state->ms->sample_shading_enable) ||
       ps->info.ps.uses_sample_shading || ps->info.ps.reads_sample_mask_in) {
      /* Disable VRS and use the rates from PS_ITER_SAMPLES if:
       *
       * 1) sample shading is enabled or per-sample interpolation is
       *    used by the fragment shader
       * 2) the fragment shader reads gl_SampleMaskIn because the
       *    16-bit sample coverage mask isn't enough for MSAA8x and
       *    2x2 coarse shading isn't enough.
       */
      vrs->pa_cl_vrs_cntl = S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE);

      /* Make sure sample shading is enabled even if only MSAA1x is
       * used because the SAMPLE_ITER combiner is in passthrough
       * mode if PS_ITER_SAMPLE is 0, and it uses the per-draw rate.
       * The default VRS rate when sample shading is enabled is 1x1.
       */
      if (!G_028A4C_PS_ITER_SAMPLE(ms->pa_sc_mode_cntl_1))
         ms->pa_sc_mode_cntl_1 |= S_028A4C_PS_ITER_SAMPLE(1);
   } else {
      vrs->pa_cl_vrs_cntl = S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_VRS_COMB_MODE_PASSTHRU);
   }
}

static uint32_t
si_conv_tess_prim_to_gs_out(enum tess_primitive_mode prim)
{
   switch (prim) {
   case TESS_PRIMITIVE_TRIANGLES:
   case TESS_PRIMITIVE_QUADS:
      return V_028A6C_TRISTRIP;
   case TESS_PRIMITIVE_ISOLINES:
      return V_028A6C_LINESTRIP;
   default:
      assert(0);
      return 0;
   }
}

static uint32_t
si_conv_gl_prim_to_gs_out(unsigned gl_prim)
{
   switch (gl_prim) {
   case SHADER_PRIM_POINTS:
      return V_028A6C_POINTLIST;
   case SHADER_PRIM_LINES:
   case SHADER_PRIM_LINE_STRIP:
   case SHADER_PRIM_LINES_ADJACENCY:
      return V_028A6C_LINESTRIP;

   case SHADER_PRIM_TRIANGLES:
   case SHADER_PRIM_TRIANGLE_STRIP_ADJACENCY:
   case SHADER_PRIM_TRIANGLE_STRIP:
   case SHADER_PRIM_QUADS:
      return V_028A6C_TRISTRIP;
   default:
      assert(0);
      return 0;
   }
}

static uint64_t
radv_dynamic_state_mask(VkDynamicState state)
{
   switch (state) {
   case VK_DYNAMIC_STATE_VIEWPORT:
   case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT:
      return RADV_DYNAMIC_VIEWPORT;
   case VK_DYNAMIC_STATE_SCISSOR:
   case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
      return RADV_DYNAMIC_SCISSOR;
   case VK_DYNAMIC_STATE_LINE_WIDTH:
      return RADV_DYNAMIC_LINE_WIDTH;
   case VK_DYNAMIC_STATE_DEPTH_BIAS:
      return RADV_DYNAMIC_DEPTH_BIAS;
   case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
      return RADV_DYNAMIC_BLEND_CONSTANTS;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
      return RADV_DYNAMIC_DEPTH_BOUNDS;
   case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
      return RADV_DYNAMIC_STENCIL_COMPARE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
      return RADV_DYNAMIC_STENCIL_WRITE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
      return RADV_DYNAMIC_STENCIL_REFERENCE;
   case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT:
      return RADV_DYNAMIC_DISCARD_RECTANGLE;
   case VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT:
      return RADV_DYNAMIC_SAMPLE_LOCATIONS;
   case VK_DYNAMIC_STATE_LINE_STIPPLE_EXT:
      return RADV_DYNAMIC_LINE_STIPPLE;
   case VK_DYNAMIC_STATE_CULL_MODE:
      return RADV_DYNAMIC_CULL_MODE;
   case VK_DYNAMIC_STATE_FRONT_FACE:
      return RADV_DYNAMIC_FRONT_FACE;
   case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
      return RADV_DYNAMIC_PRIMITIVE_TOPOLOGY;
   case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
      return RADV_DYNAMIC_DEPTH_TEST_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
      return RADV_DYNAMIC_DEPTH_WRITE_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP:
      return RADV_DYNAMIC_DEPTH_COMPARE_OP;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
      return RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
      return RADV_DYNAMIC_STENCIL_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_OP:
      return RADV_DYNAMIC_STENCIL_OP;
   case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:
      return RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE;
   case VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR:
      return RADV_DYNAMIC_FRAGMENT_SHADING_RATE;
   case VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT:
      return RADV_DYNAMIC_PATCH_CONTROL_POINTS;
   case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE:
      return RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE:
      return RADV_DYNAMIC_DEPTH_BIAS_ENABLE;
   case VK_DYNAMIC_STATE_LOGIC_OP_EXT:
      return RADV_DYNAMIC_LOGIC_OP;
   case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE:
      return RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE;
   case VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
      return RADV_DYNAMIC_COLOR_WRITE_ENABLE;
   case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:
      return RADV_DYNAMIC_VERTEX_INPUT;
   default:
      unreachable("Unhandled dynamic state");
   }
}

static bool
radv_pipeline_is_blend_enabled(const struct radv_graphics_pipeline *pipeline,
                               const struct vk_color_blend_state *cb)
{
   if (cb) {
      for (uint32_t i = 0; i < cb->attachment_count; i++) {
         if (cb->attachments[i].write_mask && cb->attachments[i].blend_enable)
            return true;
      }
   }

   return false;
}

static uint64_t
radv_pipeline_needed_dynamic_state(const struct radv_graphics_pipeline *pipeline,
                                   const struct vk_graphics_pipeline_state *state)
{
   bool has_color_att = radv_pipeline_has_color_attachments(state->rp);
   bool raster_enabled = !state->rs->rasterizer_discard_enable ||
                         (pipeline->dynamic_states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE);
   uint64_t states = RADV_DYNAMIC_ALL;

   /* Disable dynamic states that are useless to mesh shading. */
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH)) {
      if (!raster_enabled)
         return RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE;

      states &= ~(RADV_DYNAMIC_VERTEX_INPUT | RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE |
                  RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE | RADV_DYNAMIC_PRIMITIVE_TOPOLOGY);
   }

   /* Disable dynamic states that are useless when rasterization is disabled. */
   if (!raster_enabled) {
      states = RADV_DYNAMIC_PRIMITIVE_TOPOLOGY | RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE |
               RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE | RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE |
               RADV_DYNAMIC_VERTEX_INPUT;

      if (pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
         states |= RADV_DYNAMIC_PATCH_CONTROL_POINTS;

      return states;
   }

   if (!state->rs->depth_bias.enable &&
       !(pipeline->dynamic_states & RADV_DYNAMIC_DEPTH_BIAS_ENABLE))
      states &= ~RADV_DYNAMIC_DEPTH_BIAS;

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE) &&
       (!state->ds || !state->ds->depth.bounds_test.enable))
      states &= ~RADV_DYNAMIC_DEPTH_BOUNDS;

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_STENCIL_TEST_ENABLE) &&
       (!state->ds || !state->ds->stencil.test_enable))
      states &= ~(RADV_DYNAMIC_STENCIL_COMPARE_MASK | RADV_DYNAMIC_STENCIL_WRITE_MASK |
                  RADV_DYNAMIC_STENCIL_REFERENCE | RADV_DYNAMIC_STENCIL_OP);

   if (!state->dr->rectangle_count)
      states &= ~RADV_DYNAMIC_DISCARD_RECTANGLE;

   if (!state->ms || !state->ms->sample_locations_enable)
      states &= ~RADV_DYNAMIC_SAMPLE_LOCATIONS;

   if (!state->rs->line.stipple.enable)
      states &= ~RADV_DYNAMIC_LINE_STIPPLE;

   if (!radv_is_vrs_enabled(pipeline, state))
      states &= ~RADV_DYNAMIC_FRAGMENT_SHADING_RATE;

   if (!has_color_att || !radv_pipeline_is_blend_enabled(pipeline, state->cb))
      states &= ~RADV_DYNAMIC_BLEND_CONSTANTS;

   if (!has_color_att)
      states &= ~RADV_DYNAMIC_COLOR_WRITE_ENABLE;

   if (!(pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT))
      states &= ~RADV_DYNAMIC_PATCH_CONTROL_POINTS;

   return states;
}

static struct radv_ia_multi_vgt_param_helpers
radv_compute_ia_multi_vgt_param_helpers(struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_ia_multi_vgt_param_helpers ia_multi_vgt_param = {0};

   ia_multi_vgt_param.ia_switch_on_eoi = false;
   if (pipeline->base.shaders[MESA_SHADER_FRAGMENT]->info.ps.prim_id_input)
      ia_multi_vgt_param.ia_switch_on_eoi = true;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY) && pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.uses_prim_id)
      ia_multi_vgt_param.ia_switch_on_eoi = true;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      /* SWITCH_ON_EOI must be set if PrimID is used. */
      if (pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.uses_prim_id ||
          radv_get_shader(&pipeline->base, MESA_SHADER_TESS_EVAL)->info.uses_prim_id)
         ia_multi_vgt_param.ia_switch_on_eoi = true;
   }

   ia_multi_vgt_param.partial_vs_wave = false;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      /* Bug with tessellation and GS on Bonaire and older 2 SE chips. */
      if ((pdevice->rad_info.family == CHIP_TAHITI ||
           pdevice->rad_info.family == CHIP_PITCAIRN ||
           pdevice->rad_info.family == CHIP_BONAIRE) &&
          radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
         ia_multi_vgt_param.partial_vs_wave = true;
      /* Needed for 028B6C_DISTRIBUTION_MODE != 0 */
      if (pdevice->rad_info.has_distributed_tess) {
         if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
            if (pdevice->rad_info.gfx_level <= GFX8)
               ia_multi_vgt_param.partial_es_wave = true;
         } else {
            ia_multi_vgt_param.partial_vs_wave = true;
         }
      }
   }

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      /* On these chips there is the possibility of a hang if the
       * pipeline uses a GS and partial_vs_wave is not set.
       *
       * This mostly does not hit 4-SE chips, as those typically set
       * ia_switch_on_eoi and then partial_vs_wave is set for pipelines
       * with GS due to another workaround.
       *
       * Reproducer: https://bugs.freedesktop.org/show_bug.cgi?id=109242
       */
      if (pdevice->rad_info.family == CHIP_TONGA ||
          pdevice->rad_info.family == CHIP_FIJI ||
          pdevice->rad_info.family == CHIP_POLARIS10 ||
          pdevice->rad_info.family == CHIP_POLARIS11 ||
          pdevice->rad_info.family == CHIP_POLARIS12 ||
          pdevice->rad_info.family == CHIP_VEGAM) {
         ia_multi_vgt_param.partial_vs_wave = true;
      }
   }

   ia_multi_vgt_param.base =
      /* The following field was moved to VGT_SHADER_STAGES_EN in GFX9. */
      S_028AA8_MAX_PRIMGRP_IN_WAVE(pdevice->rad_info.gfx_level == GFX8 ? 2 : 0) |
      S_030960_EN_INST_OPT_BASIC(pdevice->rad_info.gfx_level >= GFX9) |
      S_030960_EN_INST_OPT_ADV(pdevice->rad_info.gfx_level >= GFX9);

   return ia_multi_vgt_param;
}

static uint32_t
radv_get_attrib_stride(const VkPipelineVertexInputStateCreateInfo *vi, uint32_t attrib_binding)
{
   for (uint32_t i = 0; i < vi->vertexBindingDescriptionCount; i++) {
      const VkVertexInputBindingDescription *input_binding = &vi->pVertexBindingDescriptions[i];

      if (input_binding->binding == attrib_binding)
         return input_binding->stride;
   }

   return 0;
}

#define ALL_GRAPHICS_LIB_FLAGS \
   (VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT | \
    VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT | \
    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT | \
    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)

static VkResult
radv_pipeline_import_graphics_info(struct radv_graphics_pipeline *pipeline,
                                   struct vk_graphics_pipeline_state *state,
                                   struct radv_pipeline_layout *layout,
                                   const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                   VkGraphicsPipelineLibraryFlagBitsEXT lib_flags)
{
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   struct radv_device *device = pipeline->base.device;
   VkResult result;

   /* Mark all states declared dynamic at pipeline creation. */
   if (pCreateInfo->pDynamicState) {
      uint32_t count = pCreateInfo->pDynamicState->dynamicStateCount;
      for (uint32_t s = 0; s < count; s++) {
         pipeline->dynamic_states |=
            radv_dynamic_state_mask(pCreateInfo->pDynamicState->pDynamicStates[s]);
      }
   }

   /* Mark all active stages at pipeline creation. */
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];

      pipeline->active_stages |= sinfo->stage;
   }

   result = vk_graphics_pipeline_state_fill(&device->vk, state, pCreateInfo, NULL, NULL, NULL,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                            &pipeline->state_data);
   if (result != VK_SUCCESS)
      return result;

   if (lib_flags == ALL_GRAPHICS_LIB_FLAGS) {
      radv_pipeline_layout_finish(device, layout);
      radv_pipeline_layout_init(device, layout, false /* independent_sets */);
   }

   if (pipeline_layout) {
      /* As explained in the specification, the application can provide a non
       * compatible pipeline layout when doing optimized linking :
       *
       *    "However, in the specific case that a final link is being
       *     performed between stages and
       *     `VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT` is specified,
       *     the application can override the pipeline layout with one that is
       *     compatible with that union but does not have the
       *     `VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT` flag set,
       *     allowing a more optimal pipeline layout to be used when
       *     generating the final pipeline."
       *
       * In that case discard whatever was imported before.
       */
      if (pCreateInfo->flags & VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT &&
          !pipeline_layout->independent_sets) {
         radv_pipeline_layout_finish(device, layout);
         radv_pipeline_layout_init(device, layout, false /* independent_sets */);
      } else {
         /* Otherwise if we include a layout that had independent_sets,
          * propagate that property.
          */
         layout->independent_sets |= pipeline_layout->independent_sets;
      }

      for (uint32_t s = 0; s < pipeline_layout->num_sets; s++) {
         if (pipeline_layout->set[s].layout == NULL)
            continue;

         radv_pipeline_layout_add_set(layout, s, pipeline_layout->set[s].layout);
      }

      layout->push_constant_size = pipeline_layout->push_constant_size;
   }

   return result;
}

static void
radv_graphics_pipeline_import_lib(struct radv_graphics_pipeline *pipeline,
                                  struct vk_graphics_pipeline_state *state,
                                  struct radv_pipeline_layout *layout,
                                  struct radv_graphics_lib_pipeline *lib)
{
   /* There should be no common blocks between a lib we import and the current
    * pipeline we're building.
    */
   assert((pipeline->active_stages & lib->base.active_stages) == 0);

   pipeline->dynamic_states |= lib->base.dynamic_states;
   pipeline->active_stages |= lib->base.active_stages;

   vk_graphics_pipeline_state_merge(state, &lib->graphics_state);

   /* Import the NIR shaders (after SPIRV->NIR). */
   for (uint32_t s = 0; s < ARRAY_SIZE(lib->base.base.shaders); s++) {
      if (!lib->base.base.retained_shaders[s].nir)
         continue;

      pipeline->base.retained_shaders[s] = lib->base.base.retained_shaders[s];
   }

   /* Import the PS epilog if present. */
   if (lib->base.ps_epilog) {
      assert(!pipeline->ps_epilog);
      pipeline->ps_epilog = radv_shader_part_ref(lib->base.ps_epilog);
   }

   /* Import the pipeline layout. */
   struct radv_pipeline_layout *lib_layout = &lib->layout;
   for (uint32_t s = 0; s < lib_layout->num_sets; s++) {
      if (!lib_layout->set[s].layout)
         continue;

      radv_pipeline_layout_add_set(layout, s, lib_layout->set[s].layout);
   }

   layout->independent_sets = lib_layout->independent_sets;
   layout->push_constant_size = MAX2(layout->push_constant_size, lib_layout->push_constant_size);
}

static void
radv_pipeline_init_input_assembly_state(struct radv_graphics_pipeline *pipeline)
{
   pipeline->ia_multi_vgt_param = radv_compute_ia_multi_vgt_param_helpers(pipeline);
}

static void
radv_pipeline_init_dynamic_state(struct radv_graphics_pipeline *pipeline,
                                 const struct vk_graphics_pipeline_state *state)
{
   uint64_t needed_states = radv_pipeline_needed_dynamic_state(pipeline, state);
   uint64_t states = needed_states;

   pipeline->dynamic_state = default_dynamic_state;
   pipeline->needed_dynamic_state = needed_states;

   states &= ~pipeline->dynamic_states;

   struct radv_dynamic_state *dynamic = &pipeline->dynamic_state;

   if (needed_states & RADV_DYNAMIC_VIEWPORT) {
      dynamic->viewport.count = state->vp->viewport_count;
      if (states & RADV_DYNAMIC_VIEWPORT) {
         typed_memcpy(dynamic->viewport.viewports, state->vp->viewports, state->vp->viewport_count);
         for (unsigned i = 0; i < dynamic->viewport.count; i++)
            radv_get_viewport_xform(&dynamic->viewport.viewports[i],
                                    dynamic->viewport.xform[i].scale, dynamic->viewport.xform[i].translate);
      }
   }

   if (needed_states & RADV_DYNAMIC_SCISSOR) {
      dynamic->scissor.count = state->vp->scissor_count;
      if (states & RADV_DYNAMIC_SCISSOR) {
         typed_memcpy(dynamic->scissor.scissors, state->vp->scissors, state->vp->scissor_count);
      }
   }

   if (states & RADV_DYNAMIC_LINE_WIDTH) {
      dynamic->line_width = state->rs->line.width;
   }

   if (states & RADV_DYNAMIC_DEPTH_BIAS) {
      dynamic->depth_bias.bias = state->rs->depth_bias.constant;
      dynamic->depth_bias.clamp = state->rs->depth_bias.clamp;
      dynamic->depth_bias.slope = state->rs->depth_bias.slope;
   }

   /* Section 9.2 of the Vulkan 1.0.15 spec says:
    *
    *    pColorBlendState is [...] NULL if the pipeline has rasterization
    *    disabled or if the subpass of the render pass the pipeline is
    *    created against does not use any color attachments.
    */
   if (states & RADV_DYNAMIC_BLEND_CONSTANTS) {
      typed_memcpy(dynamic->blend_constants, state->cb->blend_constants, 4);
   }

   if (states & RADV_DYNAMIC_CULL_MODE) {
      dynamic->cull_mode = state->rs->cull_mode;
   }

   if (states & RADV_DYNAMIC_FRONT_FACE) {
      dynamic->front_face = state->rs->front_face;
   }

   if (states & RADV_DYNAMIC_PRIMITIVE_TOPOLOGY) {
      dynamic->primitive_topology = si_translate_prim(state->ia->primitive_topology);
   }

   /* If there is no depthstencil attachment, then don't read
    * pDepthStencilState. The Vulkan spec states that pDepthStencilState may
    * be NULL in this case. Even if pDepthStencilState is non-NULL, there is
    * no need to override the depthstencil defaults in
    * radv_pipeline::dynamic_state when there is no depthstencil attachment.
    *
    * Section 9.2 of the Vulkan 1.0.15 spec says:
    *
    *    pDepthStencilState is [...] NULL if the pipeline has rasterization
    *    disabled or if the subpass of the render pass the pipeline is created
    *    against does not use a depth/stencil attachment.
    */
   if (needed_states && radv_pipeline_has_ds_attachments(state->rp)) {
      if (states & RADV_DYNAMIC_DEPTH_BOUNDS) {
         dynamic->depth_bounds.min = state->ds->depth.bounds_test.min;
         dynamic->depth_bounds.max = state->ds->depth.bounds_test.max;
      }

      if (states & RADV_DYNAMIC_STENCIL_COMPARE_MASK) {
         dynamic->stencil_compare_mask.front = state->ds->stencil.front.compare_mask;
         dynamic->stencil_compare_mask.back = state->ds->stencil.back.compare_mask;
      }

      if (states & RADV_DYNAMIC_STENCIL_WRITE_MASK) {
         dynamic->stencil_write_mask.front = state->ds->stencil.front.write_mask;
         dynamic->stencil_write_mask.back = state->ds->stencil.back.write_mask;
      }

      if (states & RADV_DYNAMIC_STENCIL_REFERENCE) {
         dynamic->stencil_reference.front = state->ds->stencil.front.reference;
         dynamic->stencil_reference.back = state->ds->stencil.back.reference;
      }

      if (states & RADV_DYNAMIC_DEPTH_TEST_ENABLE) {
         dynamic->depth_test_enable = state->ds->depth.test_enable;
      }

      if (states & RADV_DYNAMIC_DEPTH_WRITE_ENABLE) {
         dynamic->depth_write_enable = state->ds->depth.write_enable;
      }

      if (states & RADV_DYNAMIC_DEPTH_COMPARE_OP) {
         dynamic->depth_compare_op = state->ds->depth.compare_op;
      }

      if (states & RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE) {
         dynamic->depth_bounds_test_enable = state->ds->depth.bounds_test.enable;
      }

      if (states & RADV_DYNAMIC_STENCIL_TEST_ENABLE) {
         dynamic->stencil_test_enable = state->ds->stencil.test_enable;
      }

      if (states & RADV_DYNAMIC_STENCIL_OP) {
         dynamic->stencil_op.front.compare_op = state->ds->stencil.front.op.compare;
         dynamic->stencil_op.front.fail_op = state->ds->stencil.front.op.fail;
         dynamic->stencil_op.front.pass_op = state->ds->stencil.front.op.pass;
         dynamic->stencil_op.front.depth_fail_op = state->ds->stencil.front.op.depth_fail;

         dynamic->stencil_op.back.compare_op = state->ds->stencil.back.op.compare;
         dynamic->stencil_op.back.fail_op = state->ds->stencil.back.op.fail;
         dynamic->stencil_op.back.pass_op = state->ds->stencil.back.op.pass;
         dynamic->stencil_op.back.depth_fail_op = state->ds->stencil.back.op.depth_fail;
      }
   }

   if (needed_states & RADV_DYNAMIC_DISCARD_RECTANGLE) {
      dynamic->discard_rectangle.count = state->dr->rectangle_count;
      if (states & RADV_DYNAMIC_DISCARD_RECTANGLE) {
         typed_memcpy(dynamic->discard_rectangle.rectangles, state->dr->rectangles,
                     state->dr->rectangle_count);
      }
   }

   if (states & RADV_DYNAMIC_SAMPLE_LOCATIONS) {
      unsigned count = state->ms->sample_locations->per_pixel *
                       state->ms->sample_locations->grid_size.width *
                       state->ms->sample_locations->grid_size.height;

      dynamic->sample_location.per_pixel = state->ms->sample_locations->per_pixel;
      dynamic->sample_location.grid_size = state->ms->sample_locations->grid_size;
      dynamic->sample_location.count = count;
      typed_memcpy(&dynamic->sample_location.locations[0], state->ms->sample_locations->locations,
                   count);
   }

   if (states & RADV_DYNAMIC_LINE_STIPPLE) {
      dynamic->line_stipple.factor = state->rs->line.stipple.factor;
      dynamic->line_stipple.pattern = state->rs->line.stipple.pattern;
   }

   if (states & RADV_DYNAMIC_FRAGMENT_SHADING_RATE) {
      dynamic->fragment_shading_rate.size = state->fsr->fragment_size;
      for (int i = 0; i < 2; i++)
         dynamic->fragment_shading_rate.combiner_ops[i] = state->fsr->combiner_ops[i];
   }

   if (states & RADV_DYNAMIC_DEPTH_BIAS_ENABLE) {
      dynamic->depth_bias_enable = state->rs->depth_bias.enable;
   }

   if (states & RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE) {
      dynamic->primitive_restart_enable = state->ia->primitive_restart_enable;
   }

   if (states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE) {
      dynamic->rasterizer_discard_enable = state->rs->rasterizer_discard_enable;
   }

   if (radv_pipeline_has_color_attachments(state->rp) && states & RADV_DYNAMIC_LOGIC_OP) {
      if (state->cb->logic_op_enable) {
         dynamic->logic_op = si_translate_blend_logic_op(state->cb->logic_op);
      } else {
         dynamic->logic_op = V_028808_ROP3_COPY;
      }
   }

   if (states & RADV_DYNAMIC_COLOR_WRITE_ENABLE) {
      u_foreach_bit(i, state->cb->color_write_enables) {
         dynamic->color_write_enable |= 0xfu << (i * 4);
      }
   }

   if (states & RADV_DYNAMIC_PATCH_CONTROL_POINTS) {
      dynamic->patch_control_points = state->ts->patch_control_points;
   }

   pipeline->dynamic_state.mask = states;
}

static void
radv_pipeline_init_raster_state(struct radv_graphics_pipeline *pipeline,
                                const struct vk_graphics_pipeline_state *state)
{
   const struct radv_device *device = pipeline->base.device;

   pipeline->pa_su_sc_mode_cntl =
      S_028814_POLY_MODE(state->rs->polygon_mode != VK_POLYGON_MODE_FILL) |
      S_028814_POLYMODE_FRONT_PTYPE(si_translate_fill(state->rs->polygon_mode)) |
      S_028814_POLYMODE_BACK_PTYPE(si_translate_fill(state->rs->polygon_mode)) |
      S_028814_PROVOKING_VTX_LAST(state->rs->provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT);

   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      /* It should also be set if PERPENDICULAR_ENDCAP_ENA is set. */
      pipeline->pa_su_sc_mode_cntl |=
         S_028814_KEEP_TOGETHER_ENABLE(state->rs->polygon_mode != VK_POLYGON_MODE_FILL);
   }

   pipeline->pa_cl_clip_cntl =
      S_028810_DX_CLIP_SPACE_DEF(!pipeline->negative_one_to_one) |
      S_028810_ZCLIP_NEAR_DISABLE(!state->rs->depth_clip_enable) |
      S_028810_ZCLIP_FAR_DISABLE(!state->rs->depth_clip_enable) |
      S_028810_DX_LINEAR_ATTR_CLIP_ENA(1);

   pipeline->uses_conservative_overestimate =
      state->rs->conservative_mode == VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;

   pipeline->depth_clamp_mode = RADV_DEPTH_CLAMP_MODE_VIEWPORT;
   if (!state->rs->depth_clamp_enable) {
      /* For optimal performance, depth clamping should always be enabled except if the
       * application disables clamping explicitly or uses depth values outside of the [0.0, 1.0]
       * range.
       */
      if (!state->rs->depth_clip_enable ||
          device->vk.enabled_extensions.EXT_depth_range_unrestricted) {
         pipeline->depth_clamp_mode = RADV_DEPTH_CLAMP_MODE_DISABLED;
      } else {
         pipeline->depth_clamp_mode = RADV_DEPTH_CLAMP_MODE_ZERO_TO_ONE;
      }
   }
}

static struct radv_depth_stencil_state
radv_pipeline_init_depth_stencil_state(struct radv_graphics_pipeline *pipeline,
                                       const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_depth_stencil_state ds_state = {0};

   bool has_depth_attachment = state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED;

   if (has_depth_attachment) {
      /* from amdvlk: For 4xAA and 8xAA need to decompress on flush for better performance */
      ds_state.db_render_override2 |=
         S_028010_DECOMPRESS_Z_ON_FLUSH(state->ms && state->ms->rasterization_samples > 2);

      if (pdevice->rad_info.gfx_level >= GFX10_3)
         ds_state.db_render_override2 |= S_028010_CENTROID_COMPUTATION_MODE(1);
   }

   ds_state.db_render_override |= S_02800C_FORCE_HIS_ENABLE0(V_02800C_FORCE_DISABLE) |
                                  S_02800C_FORCE_HIS_ENABLE1(V_02800C_FORCE_DISABLE);

   if (pipeline->depth_clamp_mode == RADV_DEPTH_CLAMP_MODE_DISABLED)
      ds_state.db_render_override |= S_02800C_DISABLE_VIEWPORT_CLAMP(1);

   if (pdevice->rad_info.gfx_level >= GFX11) {
      unsigned max_allowed_tiles_in_wave = 0;
      unsigned num_samples = MAX2(radv_pipeline_color_samples(state),
                                  radv_pipeline_depth_samples(state));

      if (pdevice->rad_info.has_dedicated_vram) {
         if (num_samples == 8)
            max_allowed_tiles_in_wave = 7;
         else if (num_samples == 4)
            max_allowed_tiles_in_wave = 14;
      } else {
         if (num_samples == 8)
            max_allowed_tiles_in_wave = 8;
      }

      /* TODO: We may want to disable this workaround for future chips. */
      if (num_samples >= 4) {
         if (max_allowed_tiles_in_wave)
            max_allowed_tiles_in_wave--;
         else
            max_allowed_tiles_in_wave = 15;
      }

      ds_state.db_render_control |= S_028000_OREO_MODE(V_028000_OMODE_O_THEN_B) |
                                    S_028000_MAX_ALLOWED_TILES_IN_WAVE(max_allowed_tiles_in_wave);
   }

   return ds_state;
}

static void
gfx10_emit_ge_pc_alloc(struct radeon_cmdbuf *cs, enum amd_gfx_level gfx_level,
                       uint32_t oversub_pc_lines)
{
   radeon_set_uconfig_reg(
      cs, R_030980_GE_PC_ALLOC,
      S_030980_OVERSUB_EN(oversub_pc_lines > 0) | S_030980_NUM_PC_LINES(oversub_pc_lines - 1));
}

static void
radv_pipeline_init_gs_ring_state(struct radv_graphics_pipeline *pipeline, const struct gfx9_gs_info *gs)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   unsigned num_se = pdevice->rad_info.max_se;
   unsigned wave_size = 64;
   unsigned max_gs_waves = 32 * num_se; /* max 32 per SE on GCN */
   /* On GFX6-GFX7, the value comes from VGT_GS_VERTEX_REUSE = 16.
    * On GFX8+, the value comes from VGT_VERTEX_REUSE_BLOCK_CNTL = 30 (+2).
    */
   unsigned gs_vertex_reuse = (pdevice->rad_info.gfx_level >= GFX8 ? 32 : 16) * num_se;
   unsigned alignment = 256 * num_se;
   /* The maximum size is 63.999 MB per SE. */
   unsigned max_size = ((unsigned)(63.999 * 1024 * 1024) & ~255) * num_se;
   struct radv_shader_info *gs_info = &pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info;

   /* Calculate the minimum size. */
   unsigned min_esgs_ring_size =
      align(gs->vgt_esgs_ring_itemsize * 4 * gs_vertex_reuse * wave_size, alignment);
   /* These are recommended sizes, not minimum sizes. */
   unsigned esgs_ring_size =
      max_gs_waves * 2 * wave_size * gs->vgt_esgs_ring_itemsize * 4 * gs_info->gs.vertices_in;
   unsigned gsvs_ring_size = max_gs_waves * 2 * wave_size * gs_info->gs.max_gsvs_emit_size;

   min_esgs_ring_size = align(min_esgs_ring_size, alignment);
   esgs_ring_size = align(esgs_ring_size, alignment);
   gsvs_ring_size = align(gsvs_ring_size, alignment);

   if (pdevice->rad_info.gfx_level <= GFX8)
      pipeline->esgs_ring_size = CLAMP(esgs_ring_size, min_esgs_ring_size, max_size);

   pipeline->gsvs_ring_size = MIN2(gsvs_ring_size, max_size);
}

struct radv_shader *
radv_get_shader(const struct radv_pipeline *pipeline, gl_shader_stage stage)
{
   if (stage == MESA_SHADER_VERTEX) {
      if (pipeline->shaders[MESA_SHADER_VERTEX])
         return pipeline->shaders[MESA_SHADER_VERTEX];
      if (pipeline->shaders[MESA_SHADER_TESS_CTRL])
         return pipeline->shaders[MESA_SHADER_TESS_CTRL];
      if (pipeline->shaders[MESA_SHADER_GEOMETRY])
         return pipeline->shaders[MESA_SHADER_GEOMETRY];
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      if (!pipeline->shaders[MESA_SHADER_TESS_CTRL])
         return NULL;
      if (pipeline->shaders[MESA_SHADER_TESS_EVAL])
         return pipeline->shaders[MESA_SHADER_TESS_EVAL];
      if (pipeline->shaders[MESA_SHADER_GEOMETRY])
         return pipeline->shaders[MESA_SHADER_GEOMETRY];
   }
   return pipeline->shaders[stage];
}

static const struct radv_vs_output_info *
get_vs_output_info(const struct radv_graphics_pipeline *pipeline)
{
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
      if (radv_pipeline_has_ngg(pipeline))
         return &pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.outinfo;
      else
         return &pipeline->base.gs_copy_shader->info.outinfo;
   else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL))
      return &pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.outinfo;
   else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      return &pipeline->base.shaders[MESA_SHADER_MESH]->info.outinfo;
   else
      return &pipeline->base.shaders[MESA_SHADER_VERTEX]->info.outinfo;
}

static bool
radv_lower_viewport_to_zero(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   /* There should be only one deref load for VIEWPORT after lower_io_to_temporaries. */
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_load_deref)
            continue;

         nir_variable *var = nir_intrinsic_get_var(intr, 0);
         if (var->data.mode != nir_var_shader_in ||
             var->data.location != VARYING_SLOT_VIEWPORT)
            continue;

         b.cursor = nir_before_instr(instr);

         nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_imm_zero(&b, 1, 32));
         progress = true;
         break;
      }
      if (progress)
         break;
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

static nir_variable *
find_layer_out_var(nir_shader *nir)
{
   nir_variable *var = nir_find_variable_with_location(nir, nir_var_shader_out, VARYING_SLOT_LAYER);
   if (var != NULL)
      return var;

   var = nir_variable_create(nir, nir_var_shader_out, glsl_int_type(), "layer id");
   var->data.location = VARYING_SLOT_LAYER;
   var->data.interpolation = INTERP_MODE_NONE;

   return var;
}

static bool
radv_lower_multiview(nir_shader *nir)
{
   /* This pass is not suitable for mesh shaders, because it can't know
    * the mapping between API mesh shader invocations and output primitives.
    * Needs to be handled in ac_nir_lower_ngg.
    */
   if (nir->info.stage == MESA_SHADER_MESH)
      return false;

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   /* Iterate in reverse order since there should be only one deref store to POS after
    * lower_io_to_temporaries for vertex shaders and inject the layer there. For geometry shaders,
    * the layer is injected right before every emit_vertex_with_counter.
    */
   nir_variable *layer = NULL;
   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_reverse(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         if (nir->info.stage == MESA_SHADER_GEOMETRY) {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_emit_vertex_with_counter)
               continue;

            b.cursor = nir_before_instr(instr);
         } else {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_store_deref)
               continue;

            nir_variable *var = nir_intrinsic_get_var(intr, 0);
            if (var->data.mode != nir_var_shader_out || var->data.location != VARYING_SLOT_POS)
               continue;

            b.cursor = nir_after_instr(instr);
         }

         if (!layer)
            layer = find_layer_out_var(nir);

         nir_store_var(&b, layer, nir_load_view_index(&b), 1);

         /* Update outputs_written to reflect that the pass added a new output. */
         nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_LAYER);

         progress = true;
         if (nir->info.stage == MESA_SHADER_VERTEX)
            break;
      }
      if (nir->info.stage == MESA_SHADER_VERTEX && progress)
         break;
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

static bool
radv_should_export_implicit_primitive_id(const struct radv_pipeline_stage *producer,
                                         const struct radv_pipeline_stage *consumer)
{
   /* When the primitive ID is read by FS, we must ensure that it's exported by the previous vertex
    * stage because it's implicit for VS or TES (but required by the Vulkan spec for GS or MS). Note
    * that when the pipeline uses NGG, it's exported later during the lowering pass.
    */
   assert(producer->stage == MESA_SHADER_VERTEX || producer->stage == MESA_SHADER_TESS_EVAL);
   return (consumer->stage == MESA_SHADER_FRAGMENT &&
           (consumer->nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID) &&
           !(producer->nir->info.outputs_written & VARYING_BIT_PRIMITIVE_ID) &&
           !producer->info.is_ngg);
}

static bool
radv_export_implicit_primitive_id(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b;
   nir_builder_init(&b, impl);

   b.cursor = nir_after_cf_list(&impl->body);

   nir_variable *var = nir_variable_create(nir, nir_var_shader_out, glsl_int_type(), NULL);
   var->data.location = VARYING_SLOT_PRIMITIVE_ID;
   var->data.interpolation = INTERP_MODE_NONE;

   nir_store_var(&b, var, nir_load_primitive_id(&b), 1);

   /* Update outputs_written to reflect that the pass added a new output. */
   nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_ID);

   nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);

   return true;
}

static void
radv_remove_point_size(const struct radv_pipeline_key *pipeline_key,
                       nir_shader *producer, nir_shader *consumer)
{
   if ((consumer->info.inputs_read & VARYING_BIT_PSIZ) ||
       !(producer->info.outputs_written & VARYING_BIT_PSIZ))
      return;

   /* Do not remove PSIZ if the shader uses XFB because it might be stored. */
   if (producer->xfb_info)
      return;

   /* Do not remove PSIZ for vertex shaders when the topology is unknown. */
   if (producer->info.stage == MESA_SHADER_VERTEX &&
       pipeline_key->vs.topology == V_008958_DI_PT_NONE)
      return;

   /* Do not remove PSIZ if the rasterization primitive uses points. */
   if (consumer->info.stage == MESA_SHADER_FRAGMENT &&
       ((producer->info.stage == MESA_SHADER_VERTEX &&
         pipeline_key->vs.topology == V_008958_DI_PT_POINTLIST) ||
        (producer->info.stage == MESA_SHADER_TESS_EVAL && producer->info.tess.point_mode) ||
        (producer->info.stage == MESA_SHADER_GEOMETRY &&
         producer->info.gs.output_primitive == SHADER_PRIM_POINTS) ||
       (producer->info.stage == MESA_SHADER_MESH &&
        producer->info.mesh.primitive_type == SHADER_PRIM_POINTS)))
      return;

   nir_variable *var =
      nir_find_variable_with_location(producer, nir_var_shader_out, VARYING_SLOT_PSIZ);
   assert(var);

   /* Change PSIZ to a global variable which allows it to be DCE'd. */
   var->data.location = 0;
   var->data.mode = nir_var_shader_temp;

   producer->info.outputs_written &= ~VARYING_BIT_PSIZ;
   NIR_PASS_V(producer, nir_fixup_deref_modes);
   NIR_PASS(_, producer, nir_remove_dead_variables, nir_var_shader_temp, NULL);
   NIR_PASS(_, producer, nir_opt_dce);
}

static void
radv_remove_color_exports(const struct radv_pipeline_key *pipeline_key, nir_shader *nir)
{
   bool fixup_derefs = false;

   nir_foreach_shader_out_variable(var, nir) {
      int idx = var->data.location;
      idx -= FRAG_RESULT_DATA0;

      if (idx < 0)
         continue;

      unsigned col_format = (pipeline_key->ps.col_format >> (4 * idx)) & 0xf;
      unsigned cb_target_mask = (pipeline_key->ps.cb_target_mask >> (4 * idx)) & 0xf;

      if (col_format == V_028714_SPI_SHADER_ZERO ||
          (col_format == V_028714_SPI_SHADER_32_R && !cb_target_mask &&
           !pipeline_key->ps.mrt0_is_dual_src)) {
         /* Remove the color export if it's unused or in presence of holes. */
         nir->info.outputs_written &= ~BITFIELD64_BIT(var->data.location);
         var->data.location = 0;
         var->data.mode = nir_var_shader_temp;
         fixup_derefs = true;
      }
   }

   if (fixup_derefs) {
      NIR_PASS_V(nir, nir_fixup_deref_modes);
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);
      NIR_PASS(_, nir, nir_opt_dce);
   }
}

static void
merge_tess_info(struct shader_info *tes_info, struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 || tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == TESS_PRIMITIVE_UNSPECIFIED ||
          tes_info->tess._primitive_mode == TESS_PRIMITIVE_UNSPECIFIED ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;

   /* Copy the merged info back to the TCS */
   tcs_info->tess.tcs_vertices_out = tes_info->tess.tcs_vertices_out;
   tcs_info->tess.spacing = tes_info->tess.spacing;
   tcs_info->tess._primitive_mode = tes_info->tess._primitive_mode;
   tcs_info->tess.ccw = tes_info->tess.ccw;
   tcs_info->tess.point_mode = tes_info->tess.point_mode;
}

static void
radv_lower_io_to_scalar_early(nir_shader *nir, nir_variable_mode mask)
{
   bool progress = false;

   NIR_PASS(progress, nir, nir_lower_io_to_scalar_early, mask);
   if (progress) {
      /* Optimize the new vector code and then remove dead vars */
      NIR_PASS(_, nir, nir_copy_prop);
      NIR_PASS(_, nir, nir_opt_shrink_vectors);

      if (mask & nir_var_shader_out) {
         /* Optimize swizzled movs of load_const for nir_link_opt_varyings's constant propagation. */
         NIR_PASS(_, nir, nir_opt_constant_folding);

         /* For nir_link_opt_varyings's duplicate input opt */
         NIR_PASS(_, nir, nir_opt_cse);
      }

      /* Run copy-propagation to help remove dead output variables (some shaders have useless copies
       * to/from an output), so compaction later will be more effective.
       *
       * This will have been done earlier but it might not have worked because the outputs were
       * vector.
       */
      if (nir->info.stage == MESA_SHADER_TESS_CTRL)
         NIR_PASS(_, nir, nir_opt_copy_prop_vars);

      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_remove_dead_variables,
               nir_var_function_temp | nir_var_shader_in | nir_var_shader_out, NULL);
   }
}

static void
radv_pipeline_link_shaders(const struct radv_device *device,
                           nir_shader *producer, nir_shader *consumer,
                           const struct radv_pipeline_key *pipeline_key)
{
   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   bool progress;

   if (consumer->info.stage == MESA_SHADER_FRAGMENT) {
      /* Lower the viewport index to zero when the last vertex stage doesn't export it. */
      if ((consumer->info.inputs_read & VARYING_BIT_VIEWPORT) &&
          !(producer->info.outputs_written & VARYING_BIT_VIEWPORT)) {
         NIR_PASS(_, consumer, radv_lower_viewport_to_zero);
      }

      /* Export the layer in the last VGT stage if multiview is used. */
      if (pipeline_key->has_multiview_view_index &&
          !(producer->info.outputs_written & VARYING_BIT_LAYER)) {
         NIR_PASS(_, producer, radv_lower_multiview);
      }

      /* Lower the view index to map on the layer. */
      NIR_PASS(_, consumer, radv_lower_view_index, producer->info.stage == MESA_SHADER_MESH);
   }

   if (pipeline_key->optimisations_disabled)
      return;

   if (consumer->info.stage == MESA_SHADER_FRAGMENT &&
       producer->info.has_transform_feedback_varyings) {
      nir_link_xfb_varyings(producer, consumer);
   }

   nir_lower_io_arrays_to_elements(producer, consumer);
   nir_validate_shader(producer, "after nir_lower_io_arrays_to_elements");
   nir_validate_shader(consumer, "after nir_lower_io_arrays_to_elements");

   radv_lower_io_to_scalar_early(producer, nir_var_shader_out);
   radv_lower_io_to_scalar_early(consumer, nir_var_shader_in);

   /* Remove PSIZ from shaders when it's not needed.
    * This is typically produced by translation layers like Zink or D9VK.
    */
   radv_remove_point_size(pipeline_key, producer, consumer);

   if (nir_link_opt_varyings(producer, consumer)) {
      nir_validate_shader(producer, "after nir_link_opt_varyings");
      nir_validate_shader(consumer, "after nir_link_opt_varyings");

      NIR_PASS(_, consumer, nir_opt_constant_folding);
      NIR_PASS(_, consumer, nir_opt_algebraic);
      NIR_PASS(_, consumer, nir_opt_dce);
   }

   NIR_PASS(_, producer, nir_remove_dead_variables, nir_var_shader_out, NULL);
   NIR_PASS(_, consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);

   progress = nir_remove_unused_varyings(producer, consumer);

   nir_compact_varyings(producer, consumer, true);
   nir_validate_shader(producer, "after nir_compact_varyings");
   nir_validate_shader(consumer, "after nir_compact_varyings");

   if (producer->info.stage == MESA_SHADER_MESH) {
      /* nir_compact_varyings can change the location of per-vertex and per-primitive outputs */
      nir_shader_gather_info(producer, nir_shader_get_entrypoint(producer));
   }

   const bool has_geom_or_tess = consumer->info.stage == MESA_SHADER_GEOMETRY ||
                                 consumer->info.stage == MESA_SHADER_TESS_CTRL;
   const bool merged_gs = consumer->info.stage == MESA_SHADER_GEOMETRY && gfx_level >= GFX9;

   if (producer->info.stage == MESA_SHADER_TESS_CTRL ||
       producer->info.stage == MESA_SHADER_MESH ||
       (producer->info.stage == MESA_SHADER_VERTEX && has_geom_or_tess) ||
       (producer->info.stage == MESA_SHADER_TESS_EVAL && merged_gs)) {
      NIR_PASS(_, producer, nir_lower_io_to_vector, nir_var_shader_out);

      if (producer->info.stage == MESA_SHADER_TESS_CTRL)
         NIR_PASS(_, producer, nir_vectorize_tess_levels);

      NIR_PASS(_, producer, nir_opt_combine_stores, nir_var_shader_out);
   }

   if (consumer->info.stage == MESA_SHADER_GEOMETRY ||
       consumer->info.stage == MESA_SHADER_TESS_CTRL ||
       consumer->info.stage == MESA_SHADER_TESS_EVAL) {
      NIR_PASS(_, consumer, nir_lower_io_to_vector, nir_var_shader_in);
   }

   if (progress) {
      progress = false;
      NIR_PASS(progress, producer, nir_lower_global_vars_to_local);
      if (progress) {
         ac_nir_lower_indirect_derefs(producer, gfx_level);
         /* remove dead writes, which can remove input loads */
         NIR_PASS(_, producer, nir_lower_vars_to_ssa);
         NIR_PASS(_, producer, nir_opt_dce);
      }

      progress = false;
      NIR_PASS(progress, consumer, nir_lower_global_vars_to_local);
      if (progress) {
         ac_nir_lower_indirect_derefs(consumer, gfx_level);
      }
   }
}

static const gl_shader_stage graphics_shader_order[] = {
   MESA_SHADER_VERTEX,
   MESA_SHADER_TESS_CTRL,
   MESA_SHADER_TESS_EVAL,
   MESA_SHADER_GEOMETRY,

   MESA_SHADER_TASK,
   MESA_SHADER_MESH,

   MESA_SHADER_FRAGMENT,
};

static void
radv_pipeline_link_vs(const struct radv_device *device, struct radv_pipeline_stage *vs_stage,
                      struct radv_pipeline_stage *next_stage,
                      const struct radv_pipeline_key *pipeline_key)
{
   assert(vs_stage->nir->info.stage == MESA_SHADER_VERTEX);
   assert(next_stage->nir->info.stage == MESA_SHADER_TESS_CTRL ||
          next_stage->nir->info.stage == MESA_SHADER_GEOMETRY ||
          next_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

   if (radv_should_export_implicit_primitive_id(vs_stage, next_stage)) {
      NIR_PASS(_, vs_stage->nir, radv_export_implicit_primitive_id);
   }

   radv_pipeline_link_shaders(device, vs_stage->nir, next_stage->nir, pipeline_key);

   nir_foreach_shader_in_variable(var, vs_stage->nir) {
      var->data.driver_location = var->data.location;
   }

   if (next_stage->nir->info.stage == MESA_SHADER_TESS_CTRL) {
      nir_linked_io_var_info vs2tcs =
         nir_assign_linked_io_var_locations(vs_stage->nir, next_stage->nir);

      vs_stage->info.vs.num_linked_outputs = vs2tcs.num_linked_io_vars;
      next_stage->info.tcs.num_linked_inputs = vs2tcs.num_linked_io_vars;
   } else if (next_stage->nir->info.stage == MESA_SHADER_GEOMETRY) {
      nir_linked_io_var_info vs2gs =
         nir_assign_linked_io_var_locations(vs_stage->nir, next_stage->nir);

      vs_stage->info.vs.num_linked_outputs = vs2gs.num_linked_io_vars;
      next_stage->info.gs.num_linked_inputs = vs2gs.num_linked_io_vars;
   } else {
      nir_foreach_shader_out_variable(var, vs_stage->nir) {
         var->data.driver_location = var->data.location;
      }
   }
}

static void
radv_pipeline_link_tcs(const struct radv_device *device, struct radv_pipeline_stage *tcs_stage,
                       struct radv_pipeline_stage *tes_stage,
                       const struct radv_pipeline_key *pipeline_key)
{
   assert(tcs_stage->nir->info.stage == MESA_SHADER_TESS_CTRL);
   assert(tes_stage->nir->info.stage == MESA_SHADER_TESS_EVAL);

   radv_pipeline_link_shaders(device, tcs_stage->nir, tes_stage->nir, pipeline_key);

   nir_lower_patch_vertices(tes_stage->nir, tcs_stage->nir->info.tess.tcs_vertices_out, NULL);

   /* Copy TCS info into the TES info */
   merge_tess_info(&tes_stage->nir->info, &tcs_stage->nir->info);

   nir_linked_io_var_info tcs2tes =
      nir_assign_linked_io_var_locations(tcs_stage->nir, tes_stage->nir);

   tcs_stage->info.tcs.num_linked_outputs = tcs2tes.num_linked_io_vars;
   tcs_stage->info.tcs.num_linked_patch_outputs = tcs2tes.num_linked_patch_io_vars;
   tes_stage->info.tes.num_linked_inputs = tcs2tes.num_linked_io_vars;
   tes_stage->info.tes.num_linked_patch_inputs = tcs2tes.num_linked_patch_io_vars;
}

static void
radv_pipeline_link_tes(const struct radv_device *device, struct radv_pipeline_stage *tes_stage,
                       struct radv_pipeline_stage *next_stage,
                       const struct radv_pipeline_key *pipeline_key)
{
   assert(tes_stage->nir->info.stage == MESA_SHADER_TESS_EVAL);
   assert(next_stage->nir->info.stage == MESA_SHADER_GEOMETRY ||
          next_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

   if (radv_should_export_implicit_primitive_id(tes_stage, next_stage)) {
      NIR_PASS(_, tes_stage->nir, radv_export_implicit_primitive_id);
   }

   radv_pipeline_link_shaders(device, tes_stage->nir, next_stage->nir, pipeline_key);

   if (next_stage->nir->info.stage == MESA_SHADER_GEOMETRY) {
      nir_linked_io_var_info tes2gs =
         nir_assign_linked_io_var_locations(tes_stage->nir, next_stage->nir);

      tes_stage->info.tes.num_linked_outputs = tes2gs.num_linked_io_vars;
      next_stage->info.gs.num_linked_inputs = tes2gs.num_linked_io_vars;
   } else {
      nir_foreach_shader_out_variable(var, tes_stage->nir) {
         var->data.driver_location = var->data.location;
      }
   }
}

static void
radv_pipeline_link_gs(const struct radv_device *device, struct radv_pipeline_stage *gs_stage,
                      struct radv_pipeline_stage *fs_stage,
                      const struct radv_pipeline_key *pipeline_key)
{
   assert(gs_stage->nir->info.stage == MESA_SHADER_GEOMETRY);
   assert(fs_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

   radv_pipeline_link_shaders(device, gs_stage->nir, fs_stage->nir, pipeline_key);

   nir_foreach_shader_out_variable(var, gs_stage->nir) {
      var->data.driver_location = var->data.location;
   }
}

static void
radv_pipeline_link_task(const struct radv_device *device, struct radv_pipeline_stage *task_stage,
                        struct radv_pipeline_stage *mesh_stage,
                        const struct radv_pipeline_key *pipeline_key)
{
   assert(task_stage->nir->info.stage == MESA_SHADER_TASK);
   assert(mesh_stage->nir->info.stage == MESA_SHADER_MESH);

   /* Linking task and mesh shaders shouldn't do anything for now but keep it for consistency. */
   radv_pipeline_link_shaders(device, task_stage->nir, mesh_stage->nir, pipeline_key);
}

static void
radv_pipeline_link_mesh(const struct radv_device *device, struct radv_pipeline_stage *mesh_stage,
                        struct radv_pipeline_stage *fs_stage,
                        const struct radv_pipeline_key *pipeline_key)
{
   assert(mesh_stage->nir->info.stage == MESA_SHADER_MESH);
   assert(fs_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

   nir_foreach_shader_in_variable(var, fs_stage->nir) {
      /* These variables are per-primitive when used with a mesh shader. */
      if (var->data.location == VARYING_SLOT_PRIMITIVE_ID ||
          var->data.location == VARYING_SLOT_VIEWPORT ||
          var->data.location == VARYING_SLOT_LAYER) {
         var->data.per_primitive = true;
      }
   }

   radv_pipeline_link_shaders(device, mesh_stage->nir, fs_stage->nir, pipeline_key);

   /* ac_nir_lower_ngg ignores driver locations for mesh shaders, but set them to all zero just to
    * be on the safe side.
    */
   nir_foreach_shader_out_variable(var, mesh_stage->nir) {
      var->data.driver_location = 0;
   }
}

static void
radv_pipeline_link_fs(struct radv_pipeline_stage *fs_stage,
                      const struct radv_pipeline_key *pipeline_key)
{
   assert(fs_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

   if (!pipeline_key->ps.has_epilog) {
      /* Only remove color exports when the format is known. */
      radv_remove_color_exports(pipeline_key, fs_stage->nir);
   }

   nir_foreach_shader_out_variable(var, fs_stage->nir) {
      var->data.driver_location = var->data.location + var->data.index;
   }
}

static void
radv_graphics_pipeline_link(const struct radv_pipeline *pipeline,
                            const struct radv_pipeline_key *pipeline_key,
                            struct radv_pipeline_stage *stages)
{
   const struct radv_device *device = pipeline->device;

   /* Walk backwards to link */
   struct radv_pipeline_stage *next_stage = NULL;
   for (int i = ARRAY_SIZE(graphics_shader_order) - 1; i >= 0; i--) {
      gl_shader_stage s = graphics_shader_order[i];
      if (!stages[s].nir)
         continue;

      switch (s) {
      case MESA_SHADER_VERTEX:
         radv_pipeline_link_vs(device, &stages[s], next_stage, pipeline_key);
         break;
      case MESA_SHADER_TESS_CTRL:
         radv_pipeline_link_tcs(device, &stages[s], next_stage, pipeline_key);
         break;
      case MESA_SHADER_TESS_EVAL:
         radv_pipeline_link_tes(device, &stages[s], next_stage, pipeline_key);
         break;
      case MESA_SHADER_GEOMETRY:
         radv_pipeline_link_gs(device, &stages[s], next_stage, pipeline_key);
         break;
      case MESA_SHADER_TASK:
         radv_pipeline_link_task(device, &stages[s], next_stage, pipeline_key);
         break;
      case MESA_SHADER_MESH:
         radv_pipeline_link_mesh(device, &stages[s], next_stage, pipeline_key);
         break;
      case MESA_SHADER_FRAGMENT:
         radv_pipeline_link_fs(&stages[s], pipeline_key);
         break;
      default:
         unreachable("Invalid graphics shader stage");
      }

      next_stage = &stages[s];
   }
}

static struct radv_pipeline_key
radv_generate_pipeline_key(const struct radv_pipeline *pipeline, VkPipelineCreateFlags flags)
{
   struct radv_device *device = pipeline->device;
   struct radv_pipeline_key key;

   memset(&key, 0, sizeof(key));

   if (flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT)
      key.optimisations_disabled = 1;

   key.disable_aniso_single_level = device->instance->disable_aniso_single_level &&
                                    device->physical_device->rad_info.gfx_level < GFX8;

   key.image_2d_view_of_3d = device->image_2d_view_of_3d &&
                             device->physical_device->rad_info.gfx_level == GFX9;

   return key;
}

static struct radv_pipeline_key
radv_generate_graphics_pipeline_key(const struct radv_graphics_pipeline *pipeline,
                                    const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                    const struct vk_graphics_pipeline_state *state,
                                    const struct radv_blend_state *blend)
{
   struct radv_device *device = pipeline->base.device;
   const struct radv_physical_device *pdevice = device->physical_device;
   struct radv_pipeline_key key = radv_generate_pipeline_key(&pipeline->base, pCreateInfo->flags);

   key.has_multiview_view_index = !!state->rp->view_mask;

   if (pipeline->dynamic_states & RADV_DYNAMIC_VERTEX_INPUT) {
      key.vs.has_prolog = true;
   }

   /* Vertex input state */
   if (state->vi) {
      u_foreach_bit(i, state->vi->attributes_valid) {
         uint32_t binding = state->vi->attributes[i].binding;
         uint32_t offset = state->vi->attributes[i].offset;
         enum pipe_format format = vk_format_to_pipe_format(state->vi->attributes[i].format);

         key.vs.vertex_attribute_formats[i] = format;
         key.vs.vertex_attribute_bindings[i] = binding;
         key.vs.vertex_attribute_offsets[i] = offset;
         key.vs.instance_rate_divisors[i] = state->vi->bindings[binding].divisor;

         if (!(pipeline->dynamic_states & RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE)) {
            /* From the Vulkan spec 1.2.157:
             *
             * "If the bound pipeline state object was created with the
             * VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE dynamic state enabled then pStrides[i]
             * specifies the distance in bytes between two consecutive elements within the
             * corresponding buffer. In this case the VkVertexInputBindingDescription::stride state
             * from the pipeline state object is ignored."
             *
             * Make sure the vertex attribute stride is zero to avoid computing a wrong offset if
             * it's initialized to something else than zero.
             */
            key.vs.vertex_attribute_strides[i] = state->vi->bindings[binding].stride;
         }

         if (state->vi->bindings[binding].input_rate) {
            key.vs.instance_rate_inputs |= 1u << i;
         }

         const struct ac_vtx_format_info *vtx_info =
            ac_get_vtx_format_info(pdevice->rad_info.gfx_level, pdevice->rad_info.family, format);
         unsigned attrib_align =
            vtx_info->chan_byte_size ? vtx_info->chan_byte_size : vtx_info->element_size;

         /* If offset is misaligned, then the buffer offset must be too. Just skip updating
          * vertex_binding_align in this case.
          */
         if (offset % attrib_align == 0) {
            key.vs.vertex_binding_align[binding] =
               MAX2(key.vs.vertex_binding_align[binding], attrib_align);
         }
      }
   }

   if (state->ts)
      key.tcs.tess_input_vertices = state->ts->patch_control_points;

   if (state->ms && state->ms->rasterization_samples > 1) {
      uint32_t ps_iter_samples = radv_pipeline_get_ps_iter_samples(state);
      key.ps.num_samples = state->ms->rasterization_samples;
      key.ps.log2_ps_iter_samples = util_logbase2(ps_iter_samples);
   }

   key.ps.col_format = blend->spi_shader_col_format;
   key.ps.cb_target_mask = blend->cb_target_mask;
   key.ps.mrt0_is_dual_src = blend->mrt0_is_dual_src;
   if (device->physical_device->rad_info.gfx_level < GFX8) {
      key.ps.is_int8 = blend->col_format_is_int8;
      key.ps.is_int10 = blend->col_format_is_int10;
   }
   if (device->physical_device->rad_info.gfx_level >= GFX11 && state->ms) {
      key.ps.alpha_to_coverage_via_mrtz = state->ms->alpha_to_coverage_enable;
   }

   if (state->ia) {
      key.vs.topology = si_translate_prim(state->ia->primitive_topology);
   }

   if (device->physical_device->rad_info.gfx_level >= GFX10 && state->rs) {
      key.vs.provoking_vtx_last =
         state->rs->provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;
   }

   if (device->instance->debug_flags & RADV_DEBUG_DISCARD_TO_DEMOTE)
      key.ps.lower_discard_to_demote = true;

   if (device->instance->enable_mrt_output_nan_fixup)
      key.ps.enable_mrt_output_nan_fixup = blend->col_format_is_float32;


   key.ps.force_vrs_enabled = device->force_vrs_enabled;

   if (device->instance->debug_flags & RADV_DEBUG_INVARIANT_GEOM)
      key.invariant_geom = true;

   key.use_ngg = device->physical_device->use_ngg;

   if ((radv_is_vrs_enabled(pipeline, state) || device->force_vrs_enabled) &&
       (device->physical_device->rad_info.family == CHIP_NAVI21 ||
        device->physical_device->rad_info.family == CHIP_NAVI22 ||
        device->physical_device->rad_info.family == CHIP_VANGOGH))
      key.adjust_frag_coord_z = true;

   if (device->instance->disable_sinking_load_input_fs)
      key.disable_sinking_load_input_fs = true;

   if (device->primitives_generated_query)
      key.primitives_generated_query = true;

   key.ps.has_epilog = !!pipeline->ps_epilog;

   key.dynamic_patch_control_points =
      !!(pipeline->dynamic_states & RADV_DYNAMIC_PATCH_CONTROL_POINTS);

   return key;
}

static void
radv_fill_shader_info_ngg(struct radv_pipeline *pipeline,
                          const struct radv_pipeline_key *pipeline_key,
                          struct radv_pipeline_stage *stages)
{
   struct radv_device *device = pipeline->device;

   if (pipeline_key->use_ngg) {
      if (stages[MESA_SHADER_TESS_CTRL].nir) {
         stages[MESA_SHADER_TESS_EVAL].info.is_ngg = true;
      } else if (stages[MESA_SHADER_VERTEX].nir) {
         stages[MESA_SHADER_VERTEX].info.is_ngg = true;
      } else if (stages[MESA_SHADER_MESH].nir) {
         stages[MESA_SHADER_MESH].info.is_ngg = true;
      }

      if (stages[MESA_SHADER_TESS_CTRL].nir && stages[MESA_SHADER_GEOMETRY].nir &&
          stages[MESA_SHADER_GEOMETRY].nir->info.gs.invocations *
                stages[MESA_SHADER_GEOMETRY].nir->info.gs.vertices_out >
             256) {
         /* Fallback to the legacy path if tessellation is
          * enabled with extreme geometry because
          * EN_MAX_VERT_OUT_PER_GS_INSTANCE doesn't work and it
          * might hang.
          */
         stages[MESA_SHADER_TESS_EVAL].info.is_ngg = false;

         /* GFX11+ requires NGG. */
         assert(device->physical_device->rad_info.gfx_level < GFX11);
      }

      gl_shader_stage last_xfb_stage = MESA_SHADER_VERTEX;

      for (int i = MESA_SHADER_VERTEX; i <= MESA_SHADER_GEOMETRY; i++) {
         if (stages[i].nir)
            last_xfb_stage = i;
      }

      bool uses_xfb = stages[last_xfb_stage].nir &&
                      stages[last_xfb_stage].nir->xfb_info;

      if (!device->physical_device->use_ngg_streamout && uses_xfb) {
         /* GFX11+ requires NGG. */
         assert(device->physical_device->rad_info.gfx_level < GFX11);

         if (stages[MESA_SHADER_TESS_CTRL].nir)
           stages[MESA_SHADER_TESS_EVAL].info.is_ngg = false;
         else
           stages[MESA_SHADER_VERTEX].info.is_ngg = false;
      }
   }
}

static void
radv_fill_shader_info(struct radv_pipeline *pipeline,
                      struct radv_pipeline_layout *pipeline_layout,
                      const struct radv_pipeline_key *pipeline_key,
                      struct radv_pipeline_stage *stages)
{
   struct radv_device *device = pipeline->device;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (!stages[i].nir)
         continue;

      radv_nir_shader_info_init(&stages[i].info);
      radv_nir_shader_info_pass(device, stages[i].nir, pipeline_layout, pipeline_key,
                                &stages[i].info);
   }

   radv_nir_shader_info_link(device, pipeline_key, stages);
}

static void
radv_declare_pipeline_args(struct radv_device *device, struct radv_pipeline_stage *stages,
                           const struct radv_pipeline_key *pipeline_key)
{
   enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   unsigned active_stages = 0;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (stages[i].nir)
         active_stages |= (1 << i);
   }

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      stages[i].args.is_gs_copy_shader = false;
      stages[i].args.explicit_scratch_args = !radv_use_llvm_for_stage(device, i);
      stages[i].args.remap_spi_ps_input = !radv_use_llvm_for_stage(device, i);
      stages[i].args.load_grid_size_from_user_sgpr = device->load_grid_size_from_user_sgpr;
   }

   if (gfx_level >= GFX9 && stages[MESA_SHADER_TESS_CTRL].nir) {
      radv_declare_shader_args(gfx_level, pipeline_key, &stages[MESA_SHADER_TESS_CTRL].info,
                               MESA_SHADER_TESS_CTRL, true, MESA_SHADER_VERTEX,
                               &stages[MESA_SHADER_TESS_CTRL].args);
      stages[MESA_SHADER_TESS_CTRL].info.user_sgprs_locs = stages[MESA_SHADER_TESS_CTRL].args.user_sgprs_locs;
      stages[MESA_SHADER_TESS_CTRL].info.inline_push_constant_mask =
         stages[MESA_SHADER_TESS_CTRL].args.ac.inline_push_const_mask;

      stages[MESA_SHADER_VERTEX].info.user_sgprs_locs = stages[MESA_SHADER_TESS_CTRL].info.user_sgprs_locs;
      stages[MESA_SHADER_VERTEX].info.inline_push_constant_mask = stages[MESA_SHADER_TESS_CTRL].info.inline_push_constant_mask;
      stages[MESA_SHADER_VERTEX].args = stages[MESA_SHADER_TESS_CTRL].args;

      active_stages &= ~(1 << MESA_SHADER_VERTEX);
      active_stages &= ~(1 << MESA_SHADER_TESS_CTRL);
   }

   if (gfx_level >= GFX9 && stages[MESA_SHADER_GEOMETRY].nir) {
      gl_shader_stage pre_stage =
         stages[MESA_SHADER_TESS_EVAL].nir ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
      radv_declare_shader_args(gfx_level, pipeline_key, &stages[MESA_SHADER_GEOMETRY].info,
                               MESA_SHADER_GEOMETRY, true, pre_stage,
                               &stages[MESA_SHADER_GEOMETRY].args);
      stages[MESA_SHADER_GEOMETRY].info.user_sgprs_locs = stages[MESA_SHADER_GEOMETRY].args.user_sgprs_locs;
      stages[MESA_SHADER_GEOMETRY].info.inline_push_constant_mask =
         stages[MESA_SHADER_GEOMETRY].args.ac.inline_push_const_mask;

      stages[pre_stage].info.user_sgprs_locs = stages[MESA_SHADER_GEOMETRY].info.user_sgprs_locs;
      stages[pre_stage].info.inline_push_constant_mask = stages[MESA_SHADER_GEOMETRY].info.inline_push_constant_mask;
      stages[pre_stage].args = stages[MESA_SHADER_GEOMETRY].args;
      active_stages &= ~(1 << pre_stage);
      active_stages &= ~(1 << MESA_SHADER_GEOMETRY);
   }

   u_foreach_bit(i, active_stages) {
      radv_declare_shader_args(gfx_level, pipeline_key, &stages[i].info, i, false,
                               MESA_SHADER_VERTEX, &stages[i].args);
      stages[i].info.user_sgprs_locs = stages[i].args.user_sgprs_locs;
      stages[i].info.inline_push_constant_mask = stages[i].args.ac.inline_push_const_mask;
   }
}

static bool
mem_vectorize_callback(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                       unsigned num_components, nir_intrinsic_instr *low, nir_intrinsic_instr *high,
                       void *data)
{
   if (num_components > 4)
      return false;

   /* >128 bit loads are split except with SMEM */
   if (bit_size * num_components > 128)
      return false;

   uint32_t align;
   if (align_offset)
      align = 1 << (ffs(align_offset) - 1);
   else
      align = align_mul;

   switch (low->intrinsic) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_push_constant: {
      unsigned max_components;
      if (align % 4 == 0)
         max_components = NIR_MAX_VEC_COMPONENTS;
      else if (align % 2 == 0)
         max_components = 16u / bit_size;
      else
         max_components = 8u / bit_size;
      return (align % (bit_size / 8u)) == 0 && num_components <= max_components;
   }
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
      assert(nir_deref_mode_is(nir_src_as_deref(low->src[0]), nir_var_mem_shared));
      FALLTHROUGH;
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
      if (bit_size * num_components ==
          96) { /* 96 bit loads require 128 bit alignment and are split otherwise */
         return align % 16 == 0;
      } else if (bit_size == 16 && (align % 4)) {
         /* AMD hardware can't do 2-byte aligned f16vec2 loads, but they are useful for ALU
          * vectorization, because our vectorizer requires the scalar IR to already contain vectors.
          */
         return (align % 2 == 0) && num_components <= 2;
      } else {
         if (num_components == 3) {
            /* AMD hardware can't do 3-component loads except for 96-bit loads, handled above. */
            return false;
         }
         unsigned req = bit_size * num_components;
         if (req == 64 || req == 128) /* 64-bit and 128-bit loads can use ds_read2_b{32,64} */
            req /= 2u;
         return align % (req / 8u) == 0;
      }
   default:
      return false;
   }
   return false;
}

static unsigned
lower_bit_size_callback(const nir_instr *instr, void *_)
{
   struct radv_device *device = _;
   enum amd_gfx_level chip = device->physical_device->rad_info.gfx_level;

   if (instr->type != nir_instr_type_alu)
      return 0;
   nir_alu_instr *alu = nir_instr_as_alu(instr);

   /* If an instruction is not scalarized by this point,
    * it can be emitted as packed instruction */
   if (alu->dest.dest.ssa.num_components > 1)
      return 0;

   if (alu->dest.dest.ssa.bit_size & (8 | 16)) {
      unsigned bit_size = alu->dest.dest.ssa.bit_size;
      switch (alu->op) {
      case nir_op_bitfield_select:
      case nir_op_imul_high:
      case nir_op_umul_high:
         return 32;
      case nir_op_iabs:
      case nir_op_imax:
      case nir_op_umax:
      case nir_op_imin:
      case nir_op_umin:
      case nir_op_ishr:
      case nir_op_ushr:
      case nir_op_ishl:
      case nir_op_isign:
      case nir_op_uadd_sat:
      case nir_op_usub_sat:
         return (bit_size == 8 || !(chip >= GFX8 && nir_dest_is_divergent(alu->dest.dest))) ? 32
                                                                                            : 0;
      case nir_op_iadd_sat:
      case nir_op_isub_sat:
         return bit_size == 8 || !nir_dest_is_divergent(alu->dest.dest) ? 32 : 0;

      default:
         return 0;
      }
   }

   if (nir_src_bit_size(alu->src[0].src) & (8 | 16)) {
      unsigned bit_size = nir_src_bit_size(alu->src[0].src);
      switch (alu->op) {
      case nir_op_bit_count:
      case nir_op_find_lsb:
      case nir_op_ufind_msb:
      case nir_op_i2b1:
         return 32;
      case nir_op_ilt:
      case nir_op_ige:
      case nir_op_ieq:
      case nir_op_ine:
      case nir_op_ult:
      case nir_op_uge:
         return (bit_size == 8 || !(chip >= GFX8 && nir_dest_is_divergent(alu->dest.dest))) ? 32
                                                                                            : 0;
      default:
         return 0;
      }
   }

   return 0;
}

static uint8_t
opt_vectorize_callback(const nir_instr *instr, const void *_)
{
   if (instr->type != nir_instr_type_alu)
      return 0;

   const struct radv_device *device = _;
   enum amd_gfx_level chip = device->physical_device->rad_info.gfx_level;
   if (chip < GFX9)
      return 1;

   const nir_alu_instr *alu = nir_instr_as_alu(instr);
   const unsigned bit_size = alu->dest.dest.ssa.bit_size;
   if (bit_size != 16)
      return 1;

   switch (alu->op) {
   case nir_op_fadd:
   case nir_op_fsub:
   case nir_op_fmul:
   case nir_op_ffma:
   case nir_op_fdiv:
   case nir_op_flrp:
   case nir_op_fabs:
   case nir_op_fneg:
   case nir_op_fsat:
   case nir_op_fmin:
   case nir_op_fmax:
   case nir_op_iabs:
   case nir_op_iadd:
   case nir_op_iadd_sat:
   case nir_op_uadd_sat:
   case nir_op_isub:
   case nir_op_isub_sat:
   case nir_op_usub_sat:
   case nir_op_ineg:
   case nir_op_imul:
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
      return 2;
   case nir_op_ishl: /* TODO: in NIR, these have 32bit shift operands */
   case nir_op_ishr: /* while Radeon needs 16bit operands when vectorized */
   case nir_op_ushr:
   default:
      return 1;
   }
}

static nir_component_mask_t
non_uniform_access_callback(const nir_src *src, void *_)
{
   if (src->ssa->num_components == 1)
      return 0x1;
   return nir_chase_binding(*src).success ? 0x2 : 0x3;
}


VkResult
radv_upload_shaders(struct radv_device *device, struct radv_pipeline *pipeline)
{
   uint32_t code_size = 0;

   /* Compute the total code size. */
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      struct radv_shader *shader = pipeline->shaders[i];
      if (!shader)
         continue;

      code_size += align(shader->code_size, RADV_SHADER_ALLOC_ALIGNMENT);
   }

   if (pipeline->gs_copy_shader) {
      code_size += align(pipeline->gs_copy_shader->code_size, RADV_SHADER_ALLOC_ALIGNMENT);
   }

   /* Allocate memory for all shader binaries. */
   pipeline->slab = radv_pipeline_slab_create(device, pipeline, code_size);
   if (!pipeline->slab)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pipeline->slab_bo = pipeline->slab->alloc->arena->bo;

   /* Upload shader binaries. */
   uint64_t slab_va = radv_buffer_get_va(pipeline->slab_bo);
   uint32_t slab_offset = pipeline->slab->alloc->offset;
   char *slab_ptr = pipeline->slab->alloc->arena->ptr;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      struct radv_shader *shader = pipeline->shaders[i];
      if (!shader)
         continue;

      shader->va = slab_va + slab_offset;

      void *dest_ptr = slab_ptr + slab_offset;
      if (!radv_shader_binary_upload(device, shader->binary, shader, dest_ptr))
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      slab_offset += align(shader->code_size, RADV_SHADER_ALLOC_ALIGNMENT);
   }

   if (pipeline->gs_copy_shader) {
      pipeline->gs_copy_shader->va = slab_va + slab_offset;

      void *dest_ptr = slab_ptr + slab_offset;
      if (!radv_shader_binary_upload(device, pipeline->gs_copy_shader->binary,
                                     pipeline->gs_copy_shader, dest_ptr))
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

static bool
radv_consider_force_vrs(const struct radv_pipeline *pipeline, bool noop_fs,
                        const struct radv_pipeline_stage *stages,
                        gl_shader_stage last_vgt_api_stage)
{
   struct radv_device *device = pipeline->device;

   if (!device->force_vrs_enabled)
      return false;

   if (last_vgt_api_stage != MESA_SHADER_VERTEX &&
       last_vgt_api_stage != MESA_SHADER_TESS_EVAL &&
       last_vgt_api_stage != MESA_SHADER_GEOMETRY)
      return false;

   nir_shader *last_vgt_shader = stages[last_vgt_api_stage].nir;
   if (last_vgt_shader->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_SHADING_RATE))
      return false;

   /* VRS has no effect if there is no pixel shader. */
   if (noop_fs)
      return false;

   /* Do not enable if the PS uses gl_FragCoord because it breaks postprocessing in some games. */
   nir_shader *fs_shader = stages[MESA_SHADER_FRAGMENT].nir;
   if (fs_shader &&
       BITSET_TEST(fs_shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD)) {
      return false;
   }

   return true;
}

static nir_ssa_def *
radv_adjust_vertex_fetch_alpha(nir_builder *b, enum ac_vs_input_alpha_adjust alpha_adjust,
                               nir_ssa_def *alpha)
{
   if (alpha_adjust == AC_ALPHA_ADJUST_SSCALED)
      alpha = nir_f2u32(b, alpha);

   /* For the integer-like cases, do a natural sign extension.
    *
    * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0 and happen to contain 0, 1, 2, 3 as
    * the two LSBs of the exponent.
    */
   unsigned offset = alpha_adjust == AC_ALPHA_ADJUST_SNORM ? 23u : 0u;

   alpha = nir_ibfe_imm(b, alpha, offset, 2u);

   /* Convert back to the right type. */
   if (alpha_adjust == AC_ALPHA_ADJUST_SNORM) {
      alpha = nir_i2f32(b, alpha);
      alpha = nir_fmax(b, alpha, nir_imm_float(b, -1.0f));
   } else if (alpha_adjust == AC_ALPHA_ADJUST_SSCALED) {
      alpha = nir_i2f32(b, alpha);
   }

   return alpha;
}

static bool
radv_lower_vs_input(nir_shader *nir, const struct radv_physical_device *pdevice,
                    const struct radv_pipeline_key *pipeline_key)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   if (pipeline_key->vs.has_prolog)
      return false;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_load_input)
            continue;

         unsigned location = nir_intrinsic_base(intrin) - VERT_ATTRIB_GENERIC0;

         unsigned component = nir_intrinsic_component(intrin);
         unsigned num_components = intrin->dest.ssa.num_components;

         enum pipe_format attrib_format = pipeline_key->vs.vertex_attribute_formats[location];
         const struct ac_vtx_format_info *desc = ac_get_vtx_format_info(
            pdevice->rad_info.gfx_level, pdevice->rad_info.family, attrib_format);
         bool is_float =
            nir_alu_type_get_base_type(nir_intrinsic_dest_type(intrin)) == nir_type_float;

         unsigned mask = nir_ssa_def_components_read(&intrin->dest.ssa) << component;
         unsigned num_channels = MIN2(util_last_bit(mask), desc->num_channels);

         static const unsigned swizzle_normal[4] = {0, 1, 2, 3};
         static const unsigned swizzle_post_shuffle[4] = {2, 1, 0, 3};
         bool post_shuffle = G_008F0C_DST_SEL_X(desc->dst_sel) == V_008F0C_SQ_SEL_Z;
         const unsigned *swizzle = post_shuffle ? swizzle_post_shuffle : swizzle_normal;

         b.cursor = nir_after_instr(instr);
         nir_ssa_def *channels[4];

         if (post_shuffle) {
            /* Expand to load 3 components because it's shuffled like X<->Z. */
            intrin->num_components = MAX2(component + num_components, 3);
            intrin->dest.ssa.num_components = intrin->num_components;

            nir_intrinsic_set_component(intrin, 0);

            num_channels = MAX2(num_channels, 3);
         }

         for (uint32_t i = 0; i < num_components; i++) {
            unsigned idx = i + (post_shuffle ? component : 0);

            if (swizzle[i + component] < num_channels) {
               channels[i] = nir_channel(&b, &intrin->dest.ssa, swizzle[idx]);
            } else if (i + component == 3) {
               channels[i] = is_float ? nir_imm_floatN_t(&b, 1.0f, intrin->dest.ssa.bit_size)
                                      : nir_imm_intN_t(&b, 1u, intrin->dest.ssa.bit_size);
            } else {
               channels[i] = nir_imm_zero(&b, 1, intrin->dest.ssa.bit_size);
            }
         }

         if (desc->alpha_adjust != AC_ALPHA_ADJUST_NONE && component + num_components == 4) {
            unsigned idx = num_components - 1;
            channels[idx] = radv_adjust_vertex_fetch_alpha(&b, desc->alpha_adjust, channels[idx]);
         }

         nir_ssa_def *new_dest = nir_vec(&b, channels, num_components);

         nir_ssa_def_rewrite_uses_after(&intrin->dest.ssa, new_dest,
                                        new_dest->parent_instr);

         progress = true;
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

static bool
radv_lower_fs_output(nir_shader *nir, const struct radv_pipeline_key *pipeline_key)
{
   if (pipeline_key->ps.has_epilog)
      return false;

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_store_output)
            continue;

         int slot = nir_intrinsic_base(intrin) - FRAG_RESULT_DATA0;
         if (slot < 0)
            continue;

         unsigned write_mask = nir_intrinsic_write_mask(intrin);
         unsigned col_format = (pipeline_key->ps.col_format >> (4 * slot)) & 0xf;
         bool is_int8 = (pipeline_key->ps.is_int8 >> slot) & 1;
         bool is_int10 = (pipeline_key->ps.is_int10 >> slot) & 1;
         bool enable_mrt_output_nan_fixup = (pipeline_key->ps.enable_mrt_output_nan_fixup >> slot) & 1;
         bool is_16bit = intrin->src[0].ssa->bit_size == 16;

         if (col_format == V_028714_SPI_SHADER_ZERO)
            continue;

         b.cursor = nir_before_instr(instr);
         nir_ssa_def *values[4];

         /* Extract the export values. */
         for (unsigned i = 0; i < 4; i++) {
            if (write_mask & (1 << i)) {
               values[i] = nir_channel(&b, intrin->src[0].ssa, i);
            } else {
               values[i] = nir_ssa_undef(&b, 1, 32);
            }
         }

         /* Replace NaN by zero (for 32-bit float formats) to fix game bugs if requested. */
         if (enable_mrt_output_nan_fixup && !nir->info.internal && !is_16bit) {
            u_foreach_bit(i, write_mask) {
               const bool save_exact = b.exact;

               b.exact = true;
               nir_ssa_def *isnan = nir_fneu(&b, values[i], values[i]);
               b.exact = save_exact;

               values[i] = nir_bcsel(&b, isnan, nir_imm_zero(&b, 1, 32), values[i]);
            }
         }

         if (col_format == V_028714_SPI_SHADER_FP16_ABGR ||
             col_format == V_028714_SPI_SHADER_UNORM16_ABGR ||
             col_format == V_028714_SPI_SHADER_SNORM16_ABGR ||
             col_format == V_028714_SPI_SHADER_UINT16_ABGR ||
             col_format == V_028714_SPI_SHADER_SINT16_ABGR) {
            /* Convert and/or clamp the export values. */
            switch (col_format) {
            case V_028714_SPI_SHADER_UINT16_ABGR: {
               unsigned max_rgb = is_int8 ? 255 : is_int10 ? 1023 : 0;
               u_foreach_bit(i, write_mask) {
                  if (is_int8 || is_int10) {
                     values[i] = nir_umin(&b, values[i], i == 3 && is_int10 ? nir_imm_int(&b, 3u)
                                                                            : nir_imm_int(&b, max_rgb));
                  } else if (is_16bit) {
                     values[i] = nir_u2u32(&b, values[i]);
                  }
               }
               break;
            }
            case V_028714_SPI_SHADER_SINT16_ABGR: {
               unsigned max_rgb = is_int8 ? 127 : is_int10 ? 511 : 0;
               unsigned min_rgb = is_int8 ? -128 : is_int10 ? -512 : 0;
               u_foreach_bit(i, write_mask) {
                  if (is_int8 || is_int10) {
                     values[i] = nir_imin(&b, values[i], i == 3 && is_int10 ? nir_imm_int(&b, 1u)
                                                                            : nir_imm_int(&b, max_rgb));
                     values[i] = nir_imax(&b, values[i], i == 3 && is_int10 ? nir_imm_int(&b, -2u)
                                                                            : nir_imm_int(&b, min_rgb));
                  } else if (is_16bit) {
                     values[i] = nir_i2i32(&b, values[i]);
                  }
               }
               break;
            }
            case V_028714_SPI_SHADER_UNORM16_ABGR:
            case V_028714_SPI_SHADER_SNORM16_ABGR:
               u_foreach_bit(i, write_mask) {
                  if (is_16bit) {
                     values[i] = nir_f2f32(&b, values[i]);
                  }
               }
               break;
            default:
               break;
            }

            /* Only nir_pack_32_2x16_split needs 16-bit inputs. */
            bool input_16_bit = col_format == V_028714_SPI_SHADER_FP16_ABGR && is_16bit;
            unsigned new_write_mask = 0;

            /* Pack the export values. */
            for (unsigned i = 0; i < 2; i++) {
               bool enabled = (write_mask >> (i * 2)) & 0x3;

               if (!enabled) {
                  values[i] = nir_ssa_undef(&b, 1, 32);
                  continue;
               }

               nir_ssa_def *src0 = values[i * 2];
               nir_ssa_def *src1 = values[i * 2 + 1];

               if (!(write_mask & (1 << (i * 2))))
                  src0 = nir_imm_zero(&b, 1, input_16_bit ? 16 : 32);
               if (!(write_mask & (1 << (i * 2 + 1))))
                  src1 = nir_imm_zero(&b, 1, input_16_bit ? 16 : 32);

               if (col_format == V_028714_SPI_SHADER_FP16_ABGR) {
                  if (is_16bit) {
                     values[i] = nir_pack_32_2x16_split(&b, src0, src1);
                  } else {
                     values[i] = nir_pack_half_2x16_split(&b, src0, src1);
                  }
               } else if (col_format == V_028714_SPI_SHADER_UNORM16_ABGR) {
                  values[i] = nir_pack_unorm_2x16(&b, nir_vec2(&b, src0, src1));
               } else if (col_format == V_028714_SPI_SHADER_SNORM16_ABGR) {
                  values[i] = nir_pack_snorm_2x16(&b, nir_vec2(&b, src0, src1));
               } else if (col_format == V_028714_SPI_SHADER_UINT16_ABGR) {
                  values[i] = nir_pack_uint_2x16(&b, nir_vec2(&b, src0, src1));
               } else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR) {
                  values[i] = nir_pack_sint_2x16(&b, nir_vec2(&b, src0, src1));
               }

               new_write_mask |= 1 << i;
            }

            /* Update the write mask for compressed outputs. */
            nir_intrinsic_set_write_mask(intrin, new_write_mask);
            intrin->num_components = util_last_bit(new_write_mask);
         }

         nir_ssa_def *new_src = nir_vec(&b, values, intrin->num_components);

         nir_instr_rewrite_src(&intrin->instr, &intrin->src[0], nir_src_for_ssa(new_src));

         progress = true;
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

void
radv_pipeline_stage_init(const VkPipelineShaderStageCreateInfo *sinfo,
                         struct radv_pipeline_stage *out_stage, gl_shader_stage stage)
{
   const VkShaderModuleCreateInfo *minfo =
      vk_find_struct_const(sinfo->pNext, SHADER_MODULE_CREATE_INFO);
   const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *iinfo =
      vk_find_struct_const(sinfo->pNext, PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);

   if (sinfo->module == VK_NULL_HANDLE && !minfo && !iinfo)
      return;

   memset(out_stage, 0, sizeof(*out_stage));

   out_stage->stage = stage;
   out_stage->entrypoint = sinfo->pName;
   out_stage->spec_info = sinfo->pSpecializationInfo;
   out_stage->feedback.flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;

   if (sinfo->module != VK_NULL_HANDLE) {
      struct vk_shader_module *module = vk_shader_module_from_handle(sinfo->module);
      STATIC_ASSERT(sizeof(out_stage->spirv.sha1) == sizeof(module->sha1));

      out_stage->spirv.data = module->data;
      out_stage->spirv.size = module->size;
      out_stage->spirv.object = &module->base;

      if (module->nir)
         out_stage->internal_nir = module->nir;
   } else if (minfo) {
      out_stage->spirv.data = (const char *) minfo->pCode;
      out_stage->spirv.size = minfo->codeSize;
   }

   vk_pipeline_hash_shader_stage(sinfo, out_stage->shader_sha1);
}

static struct radv_shader *
radv_pipeline_create_gs_copy_shader(struct radv_pipeline *pipeline,
                                    struct radv_pipeline_stage *stages,
                                    const struct radv_pipeline_key *pipeline_key,
                                    const struct radv_pipeline_layout *pipeline_layout,
                                    bool keep_executable_info, bool keep_statistic_info)
{
   struct radv_device *device = pipeline->device;
   struct radv_shader_info info = {0};

   radv_nir_shader_info_pass(device, stages[MESA_SHADER_GEOMETRY].nir, pipeline_layout, pipeline_key,
                             &info);
   info.wave_size = 64; /* Wave32 not supported. */
   info.workgroup_size = 64; /* HW VS: separate waves, no workgroups */
   info.ballot_bit_size = 64;

   if (stages[MESA_SHADER_GEOMETRY].info.outinfo.export_clip_dists) {
      if (stages[MESA_SHADER_GEOMETRY].nir->info.outputs_written & VARYING_BIT_CLIP_DIST0)
         info.outinfo.vs_output_param_offset[VARYING_SLOT_CLIP_DIST0] = info.outinfo.param_exports++;
      if (stages[MESA_SHADER_GEOMETRY].nir->info.outputs_written & VARYING_BIT_CLIP_DIST1)
         info.outinfo.vs_output_param_offset[VARYING_SLOT_CLIP_DIST1] = info.outinfo.param_exports++;

      info.outinfo.export_clip_dists = true;
   }

   struct radv_shader_args gs_copy_args = {0};
   gs_copy_args.is_gs_copy_shader = true;
   gs_copy_args.explicit_scratch_args = !radv_use_llvm_for_stage(device, MESA_SHADER_VERTEX);
   radv_declare_shader_args(device->physical_device->rad_info.gfx_level, pipeline_key, &info,
                            MESA_SHADER_VERTEX, false, MESA_SHADER_VERTEX, &gs_copy_args);
   info.user_sgprs_locs = gs_copy_args.user_sgprs_locs;
   info.inline_push_constant_mask = gs_copy_args.ac.inline_push_const_mask;

   return radv_create_gs_copy_shader(device, stages[MESA_SHADER_GEOMETRY].nir, &info, &gs_copy_args,
                                     keep_executable_info, keep_statistic_info,
                                     pipeline_key->optimisations_disabled);
}

static void
radv_pipeline_nir_to_asm(struct radv_pipeline *pipeline, struct radv_pipeline_stage *stages,
                         const struct radv_pipeline_key *pipeline_key,
                         const struct radv_pipeline_layout *pipeline_layout,
                         bool keep_executable_info, bool keep_statistic_info,
                         gl_shader_stage last_vgt_api_stage)
{
   struct radv_device *device = pipeline->device;
   unsigned active_stages = 0;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (stages[i].nir)
         active_stages |= (1 << i);
   }

   bool pipeline_has_ngg = last_vgt_api_stage != MESA_SHADER_NONE &&
                           stages[last_vgt_api_stage].info.is_ngg;

   if (stages[MESA_SHADER_GEOMETRY].nir && !pipeline_has_ngg) {
      pipeline->gs_copy_shader =
         radv_pipeline_create_gs_copy_shader(pipeline, stages, pipeline_key, pipeline_layout,
                                             keep_executable_info, keep_statistic_info);
   }

   for (int s = MESA_VULKAN_SHADER_STAGES - 1; s >= 0; s--) {
      if (!(active_stages & (1 << s)) || pipeline->shaders[s])
         continue;

      nir_shader *shaders[2] = { stages[s].nir, NULL };
      unsigned shader_count = 1;

      /* On GFX9+, TES is merged with GS and VS is merged with TCS or GS. */
      if (device->physical_device->rad_info.gfx_level >= GFX9 &&
          (s == MESA_SHADER_TESS_CTRL || s == MESA_SHADER_GEOMETRY)) {
         gl_shader_stage pre_stage;

         if (s == MESA_SHADER_GEOMETRY && stages[MESA_SHADER_TESS_EVAL].nir) {
            pre_stage = MESA_SHADER_TESS_EVAL;
         } else {
            pre_stage = MESA_SHADER_VERTEX;
         }

         shaders[0] = stages[pre_stage].nir;
         shaders[1] = stages[s].nir;
         shader_count = 2;
      }

      int64_t stage_start = os_time_get_nano();

      pipeline->shaders[s] = radv_shader_nir_to_asm(device, &stages[s], shaders, shader_count,
                                                    pipeline_key, keep_executable_info,
                                                    keep_statistic_info);

      stages[s].feedback.duration += os_time_get_nano() - stage_start;

      active_stages &= ~(1 << shaders[0]->info.stage);
      if (shaders[1])
         active_stages &= ~(1 << shaders[1]->info.stage);
   }
}

static void
radv_pipeline_stage_retain_shader(struct radv_pipeline *pipeline, struct radv_pipeline_stage *stage)
{
   gl_shader_stage s = stage->stage;

   pipeline->retained_shaders[s].nir = nir_shader_clone(NULL, stage->nir);
}

static void
radv_pipeline_get_nir(struct radv_pipeline *pipeline, struct radv_pipeline_stage *stages,
                      const struct radv_pipeline_key *pipeline_key, bool retain_shaders)
{
   struct radv_device *device = pipeline->device;

   for (unsigned s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      if (!stages[s].entrypoint)
         continue;

      int64_t stage_start = os_time_get_nano();

      assert(retain_shaders || pipeline->shaders[s] == NULL);

      if (pipeline->retained_shaders[s].nir) {
         stages[s].nir = nir_shader_clone(NULL, pipeline->retained_shaders[s].nir);
      } else {
         stages[s].nir = radv_shader_spirv_to_nir(device, &stages[s], pipeline_key);
      }

      if (retain_shaders)
         radv_pipeline_stage_retain_shader(pipeline, &stages[s]);

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
   }
}

static void
radv_pipeline_load_retained_shaders(struct radv_pipeline *pipeline,
                                    struct radv_pipeline_stage *stages)
{
   for (uint32_t s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      if (!pipeline->retained_shaders[s].nir)
         continue;

      int64_t stage_start = os_time_get_nano();

      assert(pipeline->shaders[s] == NULL);

      stages[s].stage = s;
      stages[s].entrypoint =
         nir_shader_get_entrypoint(pipeline->retained_shaders[s].nir)->function->name;

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
      stages[s].feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
   }
}

static void
radv_postprocess_nir(struct radv_pipeline *pipeline,
                     const struct radv_pipeline_layout *pipeline_layout,
                     const struct radv_pipeline_key *pipeline_key,
                     bool pipeline_has_ngg, unsigned last_vgt_api_stage,
                     struct radv_pipeline_stage *stage)
{
   struct radv_device *device = pipeline->device;
   enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;

   /* Wave and workgroup size should already be filled. */
   assert(stage->info.wave_size && stage->info.workgroup_size);

   if (stage->stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, stage->nir, radv_lower_fs_intrinsics, stage, pipeline_key);
   }

   enum nir_lower_non_uniform_access_type lower_non_uniform_access_types =
      nir_lower_non_uniform_ubo_access | nir_lower_non_uniform_ssbo_access |
      nir_lower_non_uniform_texture_access | nir_lower_non_uniform_image_access;

   /* In practice, most shaders do not have non-uniform-qualified
    * accesses (see
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/17558#note_1475069)
    * thus a cheaper and likely to fail check is run first.
    */
   if (nir_has_non_uniform_access(stage->nir, lower_non_uniform_access_types)) {
      NIR_PASS(_, stage->nir, nir_opt_non_uniform_access);

      if (!radv_use_llvm_for_stage(device, stage->stage)) {
         nir_lower_non_uniform_access_options options = {
            .types = lower_non_uniform_access_types,
            .callback = &non_uniform_access_callback,
            .callback_data = NULL,
         };
         NIR_PASS(_, stage->nir, nir_lower_non_uniform_access, &options);
      }
   }
   NIR_PASS(_, stage->nir, nir_lower_memory_model);

   nir_load_store_vectorize_options vectorize_opts = {
      .modes = nir_var_mem_ssbo | nir_var_mem_ubo | nir_var_mem_push_const |
               nir_var_mem_shared | nir_var_mem_global,
      .callback = mem_vectorize_callback,
      .robust_modes = 0,
      /* On GFX6, read2/write2 is out-of-bounds if the offset register is negative, even if
       * the final offset is not.
       */
      .has_shared2_amd = gfx_level >= GFX7,
   };

   if (device->robust_buffer_access2) {
      vectorize_opts.robust_modes =
         nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_push_const;
   }

   bool progress = false;
   NIR_PASS(progress, stage->nir, nir_opt_load_store_vectorize, &vectorize_opts);
   if (progress) {
      NIR_PASS(_, stage->nir, nir_copy_prop);
      NIR_PASS(_, stage->nir, nir_opt_shrink_stores,
               !device->instance->disable_shrink_image_store);

      /* Gather info again, to update whether 8/16-bit are used. */
      nir_shader_gather_info(stage->nir, nir_shader_get_entrypoint(stage->nir));
   }

   NIR_PASS(_, stage->nir, radv_nir_lower_ycbcr_textures, pipeline_layout);

   if (stage->nir->info.uses_resource_info_query)
      NIR_PASS(_, stage->nir, ac_nir_lower_resinfo, gfx_level);

   NIR_PASS_V(stage->nir, radv_nir_apply_pipeline_layout, device, pipeline_layout,
              &stage->info, &stage->args);

   NIR_PASS(_, stage->nir, nir_opt_shrink_vectors);

   NIR_PASS(_, stage->nir, nir_lower_alu_width, opt_vectorize_callback, device);

   /* lower ALU operations */
   NIR_PASS(_, stage->nir, nir_lower_int64);

   NIR_PASS(_, stage->nir, nir_opt_idiv_const, 8);

   NIR_PASS(_, stage->nir, nir_lower_idiv,
            &(nir_lower_idiv_options){
               .imprecise_32bit_lowering = false,
               .allow_fp16 = gfx_level >= GFX9,
            });

   nir_move_options sink_opts = nir_move_const_undef | nir_move_copies;
   if (stage->stage != MESA_SHADER_FRAGMENT || !pipeline_key->disable_sinking_load_input_fs)
      sink_opts |= nir_move_load_input;

   NIR_PASS(_, stage->nir, nir_opt_sink, sink_opts);
   NIR_PASS(_, stage->nir, nir_opt_move,
            nir_move_load_input | nir_move_const_undef | nir_move_copies);

   /* Lower I/O intrinsics to memory instructions. */
   bool io_to_mem = radv_lower_io_to_mem(device, stage);
   bool lowered_ngg = pipeline_has_ngg && stage->stage == last_vgt_api_stage;
   if (lowered_ngg)
      radv_lower_ngg(device, stage, pipeline_key);

   NIR_PASS(_, stage->nir, ac_nir_lower_global_access);
   NIR_PASS_V(stage->nir, radv_nir_lower_abi, gfx_level, &stage->info, &stage->args, pipeline_key,
              radv_use_llvm_for_stage(device, stage->stage));
   radv_optimize_nir_algebraic(
      stage->nir, io_to_mem || lowered_ngg || stage->stage == MESA_SHADER_COMPUTE ||
      stage->stage == MESA_SHADER_TASK);

   if (stage->nir->info.bit_sizes_int & (8 | 16)) {
      if (gfx_level >= GFX8) {
         NIR_PASS(_, stage->nir, nir_convert_to_lcssa, true, true);
         nir_divergence_analysis(stage->nir);
      }

      if (nir_lower_bit_size(stage->nir, lower_bit_size_callback, device)) {
         NIR_PASS(_, stage->nir, nir_opt_constant_folding);
      }

      if (gfx_level >= GFX8)
         NIR_PASS(_, stage->nir, nir_opt_remove_phis); /* cleanup LCSSA phis */
   }
   if (((stage->nir->info.bit_sizes_int | stage->nir->info.bit_sizes_float) & 16) &&
       gfx_level >= GFX9) {
      bool separate_g16 = gfx_level >= GFX10;
      struct nir_fold_tex_srcs_options fold_srcs_options[] = {
         {
            .sampler_dims =
               ~(BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE) | BITFIELD_BIT(GLSL_SAMPLER_DIM_BUF)),
            .src_types = (1 << nir_tex_src_coord) | (1 << nir_tex_src_lod) |
                         (1 << nir_tex_src_bias) | (1 << nir_tex_src_min_lod) |
                         (1 << nir_tex_src_ms_index) |
                         (separate_g16 ? 0 : (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy)),
         },
         {
            .sampler_dims = ~BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE),
            .src_types = (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy),
         },
      };
      struct nir_fold_16bit_tex_image_options fold_16bit_options = {
         .rounding_mode = nir_rounding_mode_rtne,
         .fold_tex_dest = true,
         .fold_image_load_store_data = true,
         .fold_image_srcs = !radv_use_llvm_for_stage(device, stage->stage),
         .fold_srcs_options_count = separate_g16 ? 2 : 1,
         .fold_srcs_options = fold_srcs_options,
      };
      NIR_PASS(_, stage->nir, nir_fold_16bit_tex_image, &fold_16bit_options);

      NIR_PASS(_, stage->nir, nir_opt_vectorize, opt_vectorize_callback, device);
   }

   /* cleanup passes */
   NIR_PASS(_, stage->nir, nir_lower_alu_width, opt_vectorize_callback, device);
   NIR_PASS(_, stage->nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, stage->nir, nir_copy_prop);
   NIR_PASS(_, stage->nir, nir_opt_dce);

   sink_opts |= nir_move_comparisons | nir_move_load_ubo | nir_move_load_ssbo;
   NIR_PASS(_, stage->nir, nir_opt_sink, sink_opts);

   nir_move_options move_opts = nir_move_const_undef | nir_move_load_ubo |
                                nir_move_load_input | nir_move_comparisons | nir_move_copies;
   NIR_PASS(_, stage->nir, nir_opt_move, move_opts);
}

VkResult
radv_create_shaders(struct radv_pipeline *pipeline, struct radv_pipeline_layout *pipeline_layout,
                    struct radv_device *device, struct radv_pipeline_cache *cache,
                    const struct radv_pipeline_key *pipeline_key,
                    const VkPipelineShaderStageCreateInfo *pStages,
                    uint32_t stageCount,
                    const VkPipelineCreateFlags flags, const uint8_t *custom_hash,
                    const VkPipelineCreationFeedbackCreateInfo *creation_feedback,
                    struct radv_pipeline_shader_stack_size **stack_sizes,
                    uint32_t *num_stack_sizes,
                    gl_shader_stage *last_vgt_api_stage)
{
   const char *noop_fs_entrypoint = "noop_fs";
   unsigned char hash[20];
   bool keep_executable_info =
      (flags & VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR) ||
      device->keep_shader_info;
   bool keep_statistic_info = (flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR) ||
                              (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS) ||
                              device->keep_shader_info;
   struct radv_pipeline_stage stages[MESA_VULKAN_SHADER_STAGES] = {0};
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   bool noop_fs = false;
   VkResult result = VK_SUCCESS;
   const bool retain_shaders =
      !!(flags & VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT);

   int64_t pipeline_start = os_time_get_nano();

   for (uint32_t i = 0; i < stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pStages[i];
      gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);

      radv_pipeline_stage_init(sinfo, &stages[stage], stage);
   }

   radv_pipeline_load_retained_shaders(pipeline, stages);

   for (unsigned s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      if (!stages[s].entrypoint)
         continue;

      if (stages[s].stage < MESA_SHADER_FRAGMENT || stages[s].stage == MESA_SHADER_MESH)
         *last_vgt_api_stage = stages[s].stage;
   }

   ASSERTED bool primitive_shading =
      stages[MESA_SHADER_VERTEX].entrypoint || stages[MESA_SHADER_TESS_CTRL].entrypoint ||
      stages[MESA_SHADER_TESS_EVAL].entrypoint || stages[MESA_SHADER_GEOMETRY].entrypoint;
   ASSERTED bool mesh_shading =
      stages[MESA_SHADER_MESH].entrypoint;

   /* Primitive and mesh shading must not be mixed in the same pipeline. */
   assert(!primitive_shading || !mesh_shading);
   /* Mesh shaders are mandatory in mesh shading pipelines. */
   assert(mesh_shading == !!stages[MESA_SHADER_MESH].entrypoint);
   /* Mesh shaders always need NGG. */
   assert(!mesh_shading || pipeline_key->use_ngg);

   if (custom_hash)
      memcpy(hash, custom_hash, 20);
   else {
      radv_hash_shaders(hash, stages, pipeline_layout, pipeline_key,
                        radv_get_hash_flags(device, keep_statistic_info));
   }

   pipeline->pipeline_hash = *(uint64_t *)hash;

   bool found_in_application_cache = true;
   if (!keep_executable_info &&
       radv_create_shaders_from_pipeline_cache(device, cache, hash, pipeline,
                                               stack_sizes, num_stack_sizes,
                                               &found_in_application_cache)) {
      if (found_in_application_cache)
         pipeline_feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      result = VK_SUCCESS;
      goto done;
   }

   if (flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
      if (found_in_application_cache)
         pipeline_feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      result = VK_PIPELINE_COMPILE_REQUIRED;
      goto done;
   }

   if (pipeline->type == RADV_PIPELINE_GRAPHICS && !stages[MESA_SHADER_FRAGMENT].entrypoint) {
      nir_builder fs_b = radv_meta_init_shader(device, MESA_SHADER_FRAGMENT, "noop_fs");

      stages[MESA_SHADER_FRAGMENT] = (struct radv_pipeline_stage) {
         .stage = MESA_SHADER_FRAGMENT,
         .internal_nir = fs_b.shader,
         .entrypoint = noop_fs_entrypoint,
         .feedback = {
            .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
         },
      };

      noop_fs = true;
   }

   radv_pipeline_get_nir(pipeline, stages, pipeline_key, retain_shaders);

   if (retain_shaders) {
      result = VK_SUCCESS;
      goto done;
   }

   /* Force per-vertex VRS. */
   if (radv_consider_force_vrs(pipeline, noop_fs, stages, *last_vgt_api_stage)) {
      assert(*last_vgt_api_stage == MESA_SHADER_VERTEX ||
             *last_vgt_api_stage == MESA_SHADER_TESS_EVAL ||
             *last_vgt_api_stage == MESA_SHADER_GEOMETRY);
      nir_shader *last_vgt_shader = stages[*last_vgt_api_stage].nir;
      NIR_PASS(_, last_vgt_shader, radv_force_primitive_shading_rate, device);
   }

   bool optimize_conservatively = pipeline_key->optimisations_disabled;

   /* Determine if shaders uses NGG before linking because it's needed for some NIR pass. */
   radv_fill_shader_info_ngg(pipeline, pipeline_key, stages);

   bool pipeline_has_ngg = (stages[MESA_SHADER_VERTEX].nir && stages[MESA_SHADER_VERTEX].info.is_ngg) ||
                           (stages[MESA_SHADER_TESS_EVAL].nir && stages[MESA_SHADER_TESS_EVAL].info.is_ngg) ||
                           (stages[MESA_SHADER_MESH].nir && stages[MESA_SHADER_MESH].info.is_ngg);

   if (stages[MESA_SHADER_GEOMETRY].nir) {
      unsigned nir_gs_flags = nir_lower_gs_intrinsics_per_stream;

      if (pipeline_has_ngg) {
         nir_gs_flags |= nir_lower_gs_intrinsics_count_primitives |
                         nir_lower_gs_intrinsics_count_vertices_per_primitive |
                         nir_lower_gs_intrinsics_overwrite_incomplete;
      }

      NIR_PASS(_, stages[MESA_SHADER_GEOMETRY].nir, nir_lower_gs_intrinsics, nir_gs_flags);
   }

   radv_graphics_pipeline_link(pipeline, pipeline_key, stages);

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (stages[i].nir) {
         int64_t stage_start = os_time_get_nano();

         radv_optimize_nir(stages[i].nir, optimize_conservatively, false);

         /* Gather info again, information such as outputs_read can be out-of-date. */
         nir_shader_gather_info(stages[i].nir, nir_shader_get_entrypoint(stages[i].nir));
         radv_lower_io(device, stages[i].nir);

         stages[i].feedback.duration += os_time_get_nano() - stage_start;
      }
   }

   if (stages[MESA_SHADER_VERTEX].nir) {
      NIR_PASS(_, stages[MESA_SHADER_VERTEX].nir, radv_lower_vs_input, device->physical_device,
               pipeline_key);
   }

   if (stages[MESA_SHADER_FRAGMENT].nir && !radv_use_llvm_for_stage(device, MESA_SHADER_FRAGMENT)) {
      /* TODO: Convert the LLVM backend. */
      NIR_PASS(_, stages[MESA_SHADER_FRAGMENT].nir, radv_lower_fs_output, pipeline_key);
   }

   radv_fill_shader_info(pipeline, pipeline_layout, pipeline_key, stages);

   radv_declare_pipeline_args(device, stages, pipeline_key);

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (!stages[i].nir)
         continue;

      int64_t stage_start = os_time_get_nano();

      radv_postprocess_nir(pipeline, pipeline_layout, pipeline_key, pipeline_has_ngg,
                           *last_vgt_api_stage, &stages[i]);

      stages[i].feedback.duration += os_time_get_nano() - stage_start;

      if (radv_can_dump_shader(device, stages[i].nir, false))
            nir_print_shader(stages[i].nir, stderr);
   }

   /* Compile NIR shaders to AMD assembly. */
   radv_pipeline_nir_to_asm(pipeline, stages, pipeline_key, pipeline_layout, keep_executable_info,
                            keep_statistic_info, *last_vgt_api_stage);

   if (keep_executable_info) {
      for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
         struct radv_shader *shader = pipeline->shaders[i];
         if (!shader)
            continue;

         if (!stages[i].spirv.size)
            continue;

         shader->spirv = malloc(stages[i].spirv.size);
         memcpy(shader->spirv, stages[i].spirv.data, stages[i].spirv.size);
         shader->spirv_size = stages[i].spirv.size;
      }
   }

   /* Upload shader binaries. */
   radv_upload_shaders(device, pipeline);

   if (!keep_executable_info) {
      if (pipeline->gs_copy_shader) {
         assert(!pipeline->shaders[MESA_SHADER_COMPUTE]);
         pipeline->shaders[MESA_SHADER_COMPUTE] = pipeline->gs_copy_shader;
      }

      radv_pipeline_cache_insert_shaders(device, cache, hash, pipeline,
                                         stack_sizes ? *stack_sizes : NULL,
                                         num_stack_sizes ? *num_stack_sizes : 0);

      if (pipeline->gs_copy_shader) {
         pipeline->gs_copy_shader = pipeline->shaders[MESA_SHADER_COMPUTE];
         pipeline->shaders[MESA_SHADER_COMPUTE] = NULL;
      }
   }

   if (pipeline->gs_copy_shader) {
      free(pipeline->gs_copy_shader->binary);
      pipeline->gs_copy_shader->binary = NULL;
   }

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (pipeline->shaders[i]) {
         free(pipeline->shaders[i]->binary);
         pipeline->shaders[i]->binary = NULL;
      }

      if (stages[i].nir) {
         if (radv_can_dump_shader_stats(device, stages[i].nir) && pipeline->shaders[i]) {
            radv_dump_shader_stats(device, pipeline, i, stderr);
         }

         ralloc_free(stages[i].nir);
      }
   }

done:
   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

      uint32_t stage_count = creation_feedback->pipelineStageCreationFeedbackCount;
      assert(stage_count == 0 || stageCount == stage_count);
      for (uint32_t i = 0; i < stage_count; i++) {
         gl_shader_stage s = vk_to_mesa_shader_stage(pStages[i].stage);
         creation_feedback->pPipelineStageCreationFeedbacks[i] = stages[s].feedback;
      }
   }

   return result;
}

static uint32_t
radv_pipeline_stage_to_user_data_0(struct radv_graphics_pipeline *pipeline, gl_shader_stage stage,
                                   enum amd_gfx_level gfx_level)
{
   bool has_gs = radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY);
   bool has_tess = radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL);
   bool has_ngg = radv_pipeline_has_ngg(pipeline);

   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      return R_00B030_SPI_SHADER_USER_DATA_PS_0;
   case MESA_SHADER_VERTEX:
      if (has_tess) {
         if (gfx_level >= GFX10) {
            return R_00B430_SPI_SHADER_USER_DATA_HS_0;
         } else if (gfx_level == GFX9) {
            return R_00B430_SPI_SHADER_USER_DATA_LS_0;
         } else {
            return R_00B530_SPI_SHADER_USER_DATA_LS_0;
         }
      }

      if (has_gs) {
         if (gfx_level >= GFX10) {
            return R_00B230_SPI_SHADER_USER_DATA_GS_0;
         } else {
            return R_00B330_SPI_SHADER_USER_DATA_ES_0;
         }
      }

      if (has_ngg)
         return R_00B230_SPI_SHADER_USER_DATA_GS_0;

      return R_00B130_SPI_SHADER_USER_DATA_VS_0;
   case MESA_SHADER_GEOMETRY:
      return gfx_level == GFX9 ? R_00B330_SPI_SHADER_USER_DATA_ES_0
                               : R_00B230_SPI_SHADER_USER_DATA_GS_0;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      return R_00B900_COMPUTE_USER_DATA_0;
   case MESA_SHADER_TESS_CTRL:
      return gfx_level == GFX9 ? R_00B430_SPI_SHADER_USER_DATA_LS_0
                               : R_00B430_SPI_SHADER_USER_DATA_HS_0;
   case MESA_SHADER_TESS_EVAL:
      if (has_gs) {
         return gfx_level >= GFX10 ? R_00B230_SPI_SHADER_USER_DATA_GS_0
                                   : R_00B330_SPI_SHADER_USER_DATA_ES_0;
      } else if (has_ngg) {
         return R_00B230_SPI_SHADER_USER_DATA_GS_0;
      } else {
         return R_00B130_SPI_SHADER_USER_DATA_VS_0;
      }
   case MESA_SHADER_MESH:
      assert(has_ngg);
      return R_00B230_SPI_SHADER_USER_DATA_GS_0;
   default:
      unreachable("unknown shader");
   }
}

struct radv_bin_size_entry {
   unsigned bpp;
   VkExtent2D extent;
};

static VkExtent2D
radv_gfx9_compute_bin_size(const struct radv_graphics_pipeline *pipeline,
                           const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   static const struct radv_bin_size_entry color_size_table[][3][9] = {
      {
         /* One RB / SE */
         {
            /* One shader engine */
            {0, {128, 128}},
            {1, {64, 128}},
            {2, {32, 128}},
            {3, {16, 128}},
            {17, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Two shader engines */
            {0, {128, 128}},
            {2, {64, 128}},
            {3, {32, 128}},
            {5, {16, 128}},
            {17, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Four shader engines */
            {0, {128, 128}},
            {3, {64, 128}},
            {5, {16, 128}},
            {17, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         /* Two RB / SE */
         {
            /* One shader engine */
            {0, {128, 128}},
            {2, {64, 128}},
            {3, {32, 128}},
            {5, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Two shader engines */
            {0, {128, 128}},
            {3, {64, 128}},
            {5, {32, 128}},
            {9, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Four shader engines */
            {0, {256, 256}},
            {2, {128, 256}},
            {3, {128, 128}},
            {5, {64, 128}},
            {9, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         /* Four RB / SE */
         {
            /* One shader engine */
            {0, {128, 256}},
            {2, {128, 128}},
            {3, {64, 128}},
            {5, {32, 128}},
            {9, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Two shader engines */
            {0, {256, 256}},
            {2, {128, 256}},
            {3, {128, 128}},
            {5, {64, 128}},
            {9, {32, 128}},
            {17, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Four shader engines */
            {0, {256, 512}},
            {2, {256, 256}},
            {3, {128, 256}},
            {5, {128, 128}},
            {9, {64, 128}},
            {17, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
   };
   static const struct radv_bin_size_entry ds_size_table[][3][9] = {
      {
         // One RB / SE
         {
            // One shader engine
            {0, {128, 256}},
            {2, {128, 128}},
            {4, {64, 128}},
            {7, {32, 128}},
            {13, {16, 128}},
            {49, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Two shader engines
            {0, {256, 256}},
            {2, {128, 256}},
            {4, {128, 128}},
            {7, {64, 128}},
            {13, {32, 128}},
            {25, {16, 128}},
            {49, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Four shader engines
            {0, {256, 512}},
            {2, {256, 256}},
            {4, {128, 256}},
            {7, {128, 128}},
            {13, {64, 128}},
            {25, {16, 128}},
            {49, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         // Two RB / SE
         {
            // One shader engine
            {0, {256, 256}},
            {2, {128, 256}},
            {4, {128, 128}},
            {7, {64, 128}},
            {13, {32, 128}},
            {25, {16, 128}},
            {97, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Two shader engines
            {0, {256, 512}},
            {2, {256, 256}},
            {4, {128, 256}},
            {7, {128, 128}},
            {13, {64, 128}},
            {25, {32, 128}},
            {49, {16, 128}},
            {97, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Four shader engines
            {0, {512, 512}},
            {2, {256, 512}},
            {4, {256, 256}},
            {7, {128, 256}},
            {13, {128, 128}},
            {25, {64, 128}},
            {49, {16, 128}},
            {97, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         // Four RB / SE
         {
            // One shader engine
            {0, {256, 512}},
            {2, {256, 256}},
            {4, {128, 256}},
            {7, {128, 128}},
            {13, {64, 128}},
            {25, {32, 128}},
            {49, {16, 128}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Two shader engines
            {0, {512, 512}},
            {2, {256, 512}},
            {4, {256, 256}},
            {7, {128, 256}},
            {13, {128, 128}},
            {25, {64, 128}},
            {49, {32, 128}},
            {97, {16, 128}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Four shader engines
            {0, {512, 512}},
            {4, {256, 512}},
            {7, {256, 256}},
            {13, {128, 256}},
            {25, {128, 128}},
            {49, {64, 128}},
            {97, {16, 128}},
            {UINT_MAX, {0, 0}},
         },
      },
   };

   VkExtent2D extent = {512, 512};

   unsigned log_num_rb_per_se =
      util_logbase2_ceil(pdevice->rad_info.max_render_backends / pdevice->rad_info.max_se);
   unsigned log_num_se = util_logbase2_ceil(pdevice->rad_info.max_se);

   unsigned total_samples = 1u << G_028BE0_MSAA_NUM_SAMPLES(pipeline->ms.pa_sc_aa_config);
   unsigned ps_iter_samples = 1u << G_028804_PS_ITER_SAMPLES(pipeline->ms.db_eqaa);
   unsigned effective_samples = total_samples;
   unsigned color_bytes_per_pixel = 0;

   if (state->cb) {
      for (unsigned i = 0; i < state->rp->color_attachment_count; i++) {
         if (!state->cb->attachments[i].write_mask)
            continue;

         if (state->rp->color_attachment_formats[i] == VK_FORMAT_UNDEFINED)
            continue;

         color_bytes_per_pixel += vk_format_get_blocksize(state->rp->color_attachment_formats[i]);
      }
   }

   /* MSAA images typically don't use all samples all the time. */
   if (effective_samples >= 2 && ps_iter_samples <= 1)
      effective_samples = 2;
   color_bytes_per_pixel *= effective_samples;

   const struct radv_bin_size_entry *color_entry = color_size_table[log_num_rb_per_se][log_num_se];
   while (color_entry[1].bpp <= color_bytes_per_pixel)
      ++color_entry;

   extent = color_entry->extent;

   if (radv_pipeline_has_ds_attachments(state->rp)) {
      /* Coefficients taken from AMDVLK */
      unsigned depth_coeff = state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED ? 5 : 0;
      unsigned stencil_coeff = state->rp->stencil_attachment_format != VK_FORMAT_UNDEFINED ? 1 : 0;
      unsigned ds_bytes_per_pixel = 4 * (depth_coeff + stencil_coeff) * total_samples;

      const struct radv_bin_size_entry *ds_entry = ds_size_table[log_num_rb_per_se][log_num_se];
      while (ds_entry[1].bpp <= ds_bytes_per_pixel)
         ++ds_entry;

      if (ds_entry->extent.width * ds_entry->extent.height < extent.width * extent.height)
         extent = ds_entry->extent;
   }

   return extent;
}

static VkExtent2D
radv_gfx10_compute_bin_size(const struct radv_graphics_pipeline *pipeline,
                            const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   VkExtent2D extent = {512, 512};

   const unsigned db_tag_size = 64;
   const unsigned db_tag_count = 312;
   const unsigned color_tag_size = 1024;
   const unsigned color_tag_count = 31;
   const unsigned fmask_tag_size = 256;
   const unsigned fmask_tag_count = 44;

   const unsigned rb_count = pdevice->rad_info.max_render_backends;
   const unsigned pipe_count = MAX2(rb_count, pdevice->rad_info.num_tcc_blocks);

   const unsigned db_tag_part = (db_tag_count * rb_count / pipe_count) * db_tag_size * pipe_count;
   const unsigned color_tag_part =
      (color_tag_count * rb_count / pipe_count) * color_tag_size * pipe_count;
   const unsigned fmask_tag_part =
      (fmask_tag_count * rb_count / pipe_count) * fmask_tag_size * pipe_count;

   const unsigned total_samples =
      1u << G_028BE0_MSAA_NUM_SAMPLES(pipeline->ms.pa_sc_aa_config);
   const unsigned samples_log = util_logbase2_ceil(total_samples);

   unsigned color_bytes_per_pixel = 0;
   unsigned fmask_bytes_per_pixel = 0;

   if (state->cb) {
      for (unsigned i = 0; i < state->rp->color_attachment_count; i++) {
         if (!state->cb->attachments[i].write_mask)
            continue;

         if (state->rp->color_attachment_formats[i] == VK_FORMAT_UNDEFINED)
            continue;

         color_bytes_per_pixel += vk_format_get_blocksize(state->rp->color_attachment_formats[i]);

         if (total_samples > 1) {
            assert(samples_log <= 3);
            const unsigned fmask_array[] = {0, 1, 1, 4};
            fmask_bytes_per_pixel += fmask_array[samples_log];
         }
      }
   }

   color_bytes_per_pixel *= total_samples;
   color_bytes_per_pixel = MAX2(color_bytes_per_pixel, 1);

   const unsigned color_pixel_count_log = util_logbase2(color_tag_part / color_bytes_per_pixel);
   extent.width = 1ull << ((color_pixel_count_log + 1) / 2);
   extent.height = 1ull << (color_pixel_count_log / 2);

   if (fmask_bytes_per_pixel) {
      const unsigned fmask_pixel_count_log = util_logbase2(fmask_tag_part / fmask_bytes_per_pixel);

      const VkExtent2D fmask_extent =
         (VkExtent2D){.width = 1ull << ((fmask_pixel_count_log + 1) / 2),
                      .height = 1ull << (color_pixel_count_log / 2)};

      if (fmask_extent.width * fmask_extent.height < extent.width * extent.height)
         extent = fmask_extent;
   }

   if (radv_pipeline_has_ds_attachments(state->rp)) {
      /* Coefficients taken from AMDVLK */
      unsigned depth_coeff = state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED ? 5 : 0;
      unsigned stencil_coeff = state->rp->stencil_attachment_format != VK_FORMAT_UNDEFINED ? 1 : 0;
      unsigned db_bytes_per_pixel = (depth_coeff + stencil_coeff) * total_samples;

      const unsigned db_pixel_count_log = util_logbase2(db_tag_part / db_bytes_per_pixel);

      const VkExtent2D db_extent = (VkExtent2D){.width = 1ull << ((db_pixel_count_log + 1) / 2),
                                                .height = 1ull << (color_pixel_count_log / 2)};

      if (db_extent.width * db_extent.height < extent.width * extent.height)
         extent = db_extent;
   }

   extent.width = MAX2(extent.width, 128);
   extent.height = MAX2(extent.width, 64);

   return extent;
}

static void
radv_pipeline_init_disabled_binning_state(struct radv_graphics_pipeline *pipeline,
                                          const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint32_t pa_sc_binner_cntl_0 = S_028C44_BINNING_MODE(V_028C44_DISABLE_BINNING_USE_LEGACY_SC) |
                                  S_028C44_DISABLE_START_OF_PRIM(1);

   if (pdevice->rad_info.gfx_level >= GFX10) {
      unsigned min_bytes_per_pixel = 0;

      if (state->cb) {
         for (unsigned i = 0; i < state->rp->color_attachment_count; i++) {
            if (!state->cb->attachments[i].write_mask)
               continue;

            if (state->rp->color_attachment_formats[i] == VK_FORMAT_UNDEFINED)
               continue;

            unsigned bytes = vk_format_get_blocksize(state->rp->color_attachment_formats[i]);
            if (!min_bytes_per_pixel || bytes < min_bytes_per_pixel)
               min_bytes_per_pixel = bytes;
         }
      }

      pa_sc_binner_cntl_0 =
         S_028C44_BINNING_MODE(V_028C44_DISABLE_BINNING_USE_NEW_SC) | S_028C44_BIN_SIZE_X(0) |
         S_028C44_BIN_SIZE_Y(0) | S_028C44_BIN_SIZE_X_EXTEND(2) |       /* 128 */
         S_028C44_BIN_SIZE_Y_EXTEND(min_bytes_per_pixel <= 4 ? 2 : 1) | /* 128 or 64 */
         S_028C44_DISABLE_START_OF_PRIM(1);
   }

   pipeline->binning.pa_sc_binner_cntl_0 = pa_sc_binner_cntl_0;
}

static void
radv_pipeline_init_binning_state(struct radv_graphics_pipeline *pipeline,
                                 const struct radv_blend_state *blend,
                                 const struct vk_graphics_pipeline_state *state)
{
   const struct radv_device *device = pipeline->base.device;

   if (device->physical_device->rad_info.gfx_level < GFX9)
      return;

   VkExtent2D bin_size;
   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      bin_size = radv_gfx10_compute_bin_size(pipeline, state);
   } else if (device->physical_device->rad_info.gfx_level == GFX9) {
      bin_size = radv_gfx9_compute_bin_size(pipeline, state);
   } else
      unreachable("Unhandled generation for binning bin size calculation");

   if (device->pbb_allowed && bin_size.width && bin_size.height) {
      struct radv_binning_settings *settings = &device->physical_device->binning_settings;

      const uint32_t pa_sc_binner_cntl_0 =
         S_028C44_BINNING_MODE(V_028C44_BINNING_ALLOWED) |
         S_028C44_BIN_SIZE_X(bin_size.width == 16) | S_028C44_BIN_SIZE_Y(bin_size.height == 16) |
         S_028C44_BIN_SIZE_X_EXTEND(util_logbase2(MAX2(bin_size.width, 32)) - 5) |
         S_028C44_BIN_SIZE_Y_EXTEND(util_logbase2(MAX2(bin_size.height, 32)) - 5) |
         S_028C44_CONTEXT_STATES_PER_BIN(settings->context_states_per_bin - 1) |
         S_028C44_PERSISTENT_STATES_PER_BIN(settings->persistent_states_per_bin - 1) |
         S_028C44_DISABLE_START_OF_PRIM(1) |
         S_028C44_FPOVS_PER_BATCH(settings->fpovs_per_batch) | S_028C44_OPTIMAL_BIN_SELECTION(1);

      pipeline->binning.pa_sc_binner_cntl_0 = pa_sc_binner_cntl_0;
   } else
      radv_pipeline_init_disabled_binning_state(pipeline, state);
}

static void
radv_pipeline_emit_depth_stencil_state(struct radeon_cmdbuf *ctx_cs,
                                       const struct radv_depth_stencil_state *ds_state)
{
   radeon_set_context_reg(ctx_cs, R_028000_DB_RENDER_CONTROL, ds_state->db_render_control);

   radeon_set_context_reg_seq(ctx_cs, R_02800C_DB_RENDER_OVERRIDE, 2);
   radeon_emit(ctx_cs, ds_state->db_render_override);
   radeon_emit(ctx_cs, ds_state->db_render_override2);
}

static void
radv_pipeline_emit_blend_state(struct radeon_cmdbuf *ctx_cs,
                               const struct radv_graphics_pipeline *pipeline,
                               const struct radv_blend_state *blend)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;

   radeon_set_context_reg_seq(ctx_cs, R_028780_CB_BLEND0_CONTROL, 8);
   radeon_emit_array(ctx_cs, blend->cb_blend_control, 8);
   radeon_set_context_reg(ctx_cs, R_028B70_DB_ALPHA_TO_MASK, blend->db_alpha_to_mask);

   if (pdevice->rad_info.has_rbplus) {

      radeon_set_context_reg_seq(ctx_cs, R_028760_SX_MRT0_BLEND_OPT, 8);
      radeon_emit_array(ctx_cs, blend->sx_mrt_blend_opt, 8);
   }

   radeon_set_context_reg(ctx_cs, R_028714_SPI_SHADER_COL_FORMAT, blend->spi_shader_col_format);

   radeon_set_context_reg(ctx_cs, R_02823C_CB_SHADER_MASK, blend->cb_shader_mask);
}

static void
radv_pipeline_emit_raster_state(struct radeon_cmdbuf *ctx_cs,
                                const struct radv_graphics_pipeline *pipeline,
                                const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const VkConservativeRasterizationModeEXT mode = state->rs->conservative_mode;
   uint32_t pa_sc_conservative_rast = S_028C4C_NULL_SQUAD_AA_MASK_ENABLE(1);

   if (pdevice->rad_info.gfx_level >= GFX9) {
      /* Conservative rasterization. */
      if (mode != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT) {
         pa_sc_conservative_rast = S_028C4C_PREZ_AA_MASK_ENABLE(1) | S_028C4C_POSTZ_AA_MASK_ENABLE(1) |
                                   S_028C4C_CENTROID_SAMPLE_OVERRIDE(1);

         if (mode == VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT) {
            pa_sc_conservative_rast |=
               S_028C4C_OVER_RAST_ENABLE(1) | S_028C4C_OVER_RAST_SAMPLE_SELECT(0) |
               S_028C4C_UNDER_RAST_ENABLE(0) | S_028C4C_UNDER_RAST_SAMPLE_SELECT(1) |
               S_028C4C_PBB_UNCERTAINTY_REGION_ENABLE(1);
         } else {
            assert(mode == VK_CONSERVATIVE_RASTERIZATION_MODE_UNDERESTIMATE_EXT);
            pa_sc_conservative_rast |=
               S_028C4C_OVER_RAST_ENABLE(0) | S_028C4C_OVER_RAST_SAMPLE_SELECT(1) |
               S_028C4C_UNDER_RAST_ENABLE(1) | S_028C4C_UNDER_RAST_SAMPLE_SELECT(0) |
               S_028C4C_PBB_UNCERTAINTY_REGION_ENABLE(0);
         }
      }

      radeon_set_context_reg(ctx_cs, R_028C4C_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
                             pa_sc_conservative_rast);
   }
}

static void
radv_pipeline_emit_multisample_state(struct radeon_cmdbuf *ctx_cs,
                                     const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct radv_multisample_state *ms = &pipeline->ms;

   radeon_set_context_reg_seq(ctx_cs, R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 2);
   radeon_emit(ctx_cs, ms->pa_sc_aa_mask[0]);
   radeon_emit(ctx_cs, ms->pa_sc_aa_mask[1]);

   radeon_set_context_reg(ctx_cs, R_028804_DB_EQAA, ms->db_eqaa);
   radeon_set_context_reg(ctx_cs, R_028BE0_PA_SC_AA_CONFIG, ms->pa_sc_aa_config);

   radeon_set_context_reg_seq(ctx_cs, R_028A48_PA_SC_MODE_CNTL_0, 2);
   radeon_emit(ctx_cs, ms->pa_sc_mode_cntl_0);
   radeon_emit(ctx_cs, ms->pa_sc_mode_cntl_1);

   /* The exclusion bits can be set to improve rasterization efficiency
    * if no sample lies on the pixel boundary (-8 sample offset). It's
    * currently always TRUE because the driver doesn't support 16 samples.
    */
   bool exclusion = pdevice->rad_info.gfx_level >= GFX7;
   radeon_set_context_reg(
      ctx_cs, R_02882C_PA_SU_PRIM_FILTER_CNTL,
      S_02882C_XMAX_RIGHT_EXCLUSION(exclusion) | S_02882C_YMAX_BOTTOM_EXCLUSION(exclusion));
}

static void
radv_pipeline_emit_vgt_gs_mode(struct radeon_cmdbuf *ctx_cs,
                               const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   const struct radv_shader *vs = pipeline->base.shaders[MESA_SHADER_TESS_EVAL]
                                  ? pipeline->base.shaders[MESA_SHADER_TESS_EVAL]
                                  : pipeline->base.shaders[MESA_SHADER_VERTEX];
   unsigned vgt_primitiveid_en = 0;
   uint32_t vgt_gs_mode = 0;

   if (radv_pipeline_has_ngg(pipeline))
      return;

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      const struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];

      vgt_gs_mode = ac_vgt_gs_mode(gs->info.gs.vertices_out, pdevice->rad_info.gfx_level);
   } else if (outinfo->export_prim_id || vs->info.uses_prim_id) {
      vgt_gs_mode = S_028A40_MODE(V_028A40_GS_SCENARIO_A);
      vgt_primitiveid_en |= S_028A84_PRIMITIVEID_EN(1);
   }

   radeon_set_context_reg(ctx_cs, R_028A84_VGT_PRIMITIVEID_EN, vgt_primitiveid_en);
   radeon_set_context_reg(ctx_cs, R_028A40_VGT_GS_MODE, vgt_gs_mode);
}

static void
radv_pipeline_emit_hw_vs(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                         const struct radv_graphics_pipeline *pipeline, const struct radv_shader *shader)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg_seq(cs, R_00B120_SPI_SHADER_PGM_LO_VS, 4);
   radeon_emit(cs, va >> 8);
   radeon_emit(cs, S_00B124_MEM_BASE(va >> 40));
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);

   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   unsigned clip_dist_mask, cull_dist_mask, total_mask;
   clip_dist_mask = outinfo->clip_dist_mask;
   cull_dist_mask = outinfo->cull_dist_mask;
   total_mask = clip_dist_mask | cull_dist_mask;

   bool misc_vec_ena = outinfo->writes_pointsize || outinfo->writes_layer ||
                       outinfo->writes_viewport_index || outinfo->writes_primitive_shading_rate;
   unsigned spi_vs_out_config, nparams;

   /* VS is required to export at least one param. */
   nparams = MAX2(outinfo->param_exports, 1);
   spi_vs_out_config = S_0286C4_VS_EXPORT_COUNT(nparams - 1);

   if (pdevice->rad_info.gfx_level >= GFX10) {
      spi_vs_out_config |= S_0286C4_NO_PC_EXPORT(outinfo->param_exports == 0);
   }

   radeon_set_context_reg(ctx_cs, R_0286C4_SPI_VS_OUT_CONFIG, spi_vs_out_config);

   radeon_set_context_reg(
      ctx_cs, R_02870C_SPI_SHADER_POS_FORMAT,
      S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
         S_02870C_POS1_EXPORT_FORMAT(outinfo->pos_exports > 1 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS2_EXPORT_FORMAT(outinfo->pos_exports > 2 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS3_EXPORT_FORMAT(outinfo->pos_exports > 3 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE));

   radeon_set_context_reg(ctx_cs, R_02881C_PA_CL_VS_OUT_CNTL,
                          S_02881C_USE_VTX_POINT_SIZE(outinfo->writes_pointsize) |
                             S_02881C_USE_VTX_RENDER_TARGET_INDX(outinfo->writes_layer) |
                             S_02881C_USE_VTX_VIEWPORT_INDX(outinfo->writes_viewport_index) |
                             S_02881C_USE_VTX_VRS_RATE(outinfo->writes_primitive_shading_rate) |
                             S_02881C_VS_OUT_MISC_VEC_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0f) != 0) |
                             S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xf0) != 0) |
                             total_mask << 8 | clip_dist_mask);

   if (pdevice->rad_info.gfx_level <= GFX8)
      radeon_set_context_reg(ctx_cs, R_028AB4_VGT_REUSE_OFF, outinfo->writes_viewport_index);

   unsigned late_alloc_wave64, cu_mask;
   ac_compute_late_alloc(&pdevice->rad_info, false, false, shader->config.scratch_bytes_per_wave > 0,
                         &late_alloc_wave64, &cu_mask);

   if (pdevice->rad_info.gfx_level >= GFX7) {
      if (pdevice->rad_info.gfx_level >= GFX10) {
         ac_set_reg_cu_en(cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS,
                          S_00B118_CU_EN(cu_mask) | S_00B118_WAVE_LIMIT(0x3F),
                          C_00B118_CU_EN, 0, &pdevice->rad_info,
                          (void*)gfx10_set_sh_reg_idx3);
      } else {
         radeon_set_sh_reg_idx(pdevice, cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS, 3,
                               S_00B118_CU_EN(cu_mask) | S_00B118_WAVE_LIMIT(0x3F));
      }
      radeon_set_sh_reg(cs, R_00B11C_SPI_SHADER_LATE_ALLOC_VS, S_00B11C_LIMIT(late_alloc_wave64));
   }
   if (pdevice->rad_info.gfx_level >= GFX10) {
      uint32_t oversub_pc_lines = late_alloc_wave64 ? pdevice->rad_info.pc_lines / 4 : 0;
      gfx10_emit_ge_pc_alloc(cs, pdevice->rad_info.gfx_level, oversub_pc_lines);
   }
}

static void
radv_pipeline_emit_hw_es(struct radeon_cmdbuf *cs, const struct radv_graphics_pipeline *pipeline,
                         const struct radv_shader *shader)
{
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg_seq(cs, R_00B320_SPI_SHADER_PGM_LO_ES, 4);
   radeon_emit(cs, va >> 8);
   radeon_emit(cs, S_00B324_MEM_BASE(va >> 40));
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);
}

static void
radv_pipeline_emit_hw_ls(struct radeon_cmdbuf *cs, const struct radv_graphics_pipeline *pipeline,
                         const struct radv_shader *shader)
{
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg(cs, R_00B520_SPI_SHADER_PGM_LO_LS, va >> 8);

   radeon_set_sh_reg(cs, R_00B528_SPI_SHADER_PGM_RSRC1_LS, shader->config.rsrc1);
}

static void
radv_pipeline_emit_hw_ngg(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                          const struct radv_graphics_pipeline *pipeline,
                          const struct radv_shader *shader)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint64_t va = radv_shader_get_va(shader);
   gl_shader_stage es_type =
      radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH) ? MESA_SHADER_MESH :
      radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL) ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
   struct radv_shader *es = pipeline->base.shaders[es_type];
   const struct gfx10_ngg_info *ngg_state = &shader->info.ngg_info;

   radeon_set_sh_reg(cs, R_00B320_SPI_SHADER_PGM_LO_ES, va >> 8);

   radeon_set_sh_reg_seq(cs, R_00B228_SPI_SHADER_PGM_RSRC1_GS, 2);
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);

   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   unsigned clip_dist_mask, cull_dist_mask, total_mask;
   clip_dist_mask = outinfo->clip_dist_mask;
   cull_dist_mask = outinfo->cull_dist_mask;
   total_mask = clip_dist_mask | cull_dist_mask;

   bool misc_vec_ena = outinfo->writes_pointsize || outinfo->writes_layer ||
                       outinfo->writes_viewport_index || outinfo->writes_primitive_shading_rate;
   bool es_enable_prim_id = outinfo->export_prim_id || (es && es->info.uses_prim_id);
   bool break_wave_at_eoi = false;
   unsigned ge_cntl;

   if (es_type == MESA_SHADER_TESS_EVAL) {
      struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];

      if (es_enable_prim_id || (gs && gs->info.uses_prim_id))
         break_wave_at_eoi = true;
   }

   bool no_pc_export = outinfo->param_exports == 0 && outinfo->prim_param_exports == 0;
   unsigned num_params = MAX2(outinfo->param_exports, 1);
   unsigned num_prim_params = outinfo->prim_param_exports;
   radeon_set_context_reg(
      ctx_cs, R_0286C4_SPI_VS_OUT_CONFIG,
      S_0286C4_VS_EXPORT_COUNT(num_params - 1) |
      S_0286C4_PRIM_EXPORT_COUNT(num_prim_params) |
      S_0286C4_NO_PC_EXPORT(no_pc_export));

   unsigned idx_format = V_028708_SPI_SHADER_1COMP;
   if (outinfo->writes_layer_per_primitive ||
       outinfo->writes_viewport_index_per_primitive ||
       outinfo->writes_primitive_shading_rate_per_primitive)
      idx_format = V_028708_SPI_SHADER_2COMP;

   radeon_set_context_reg(ctx_cs, R_028708_SPI_SHADER_IDX_FORMAT,
                          S_028708_IDX0_EXPORT_FORMAT(idx_format));
   radeon_set_context_reg(
      ctx_cs, R_02870C_SPI_SHADER_POS_FORMAT,
      S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
         S_02870C_POS1_EXPORT_FORMAT(outinfo->pos_exports > 1 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS2_EXPORT_FORMAT(outinfo->pos_exports > 2 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS3_EXPORT_FORMAT(outinfo->pos_exports > 3 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE));

   radeon_set_context_reg(ctx_cs, R_02881C_PA_CL_VS_OUT_CNTL,
                          S_02881C_USE_VTX_POINT_SIZE(outinfo->writes_pointsize) |
                             S_02881C_USE_VTX_RENDER_TARGET_INDX(outinfo->writes_layer) |
                             S_02881C_USE_VTX_VIEWPORT_INDX(outinfo->writes_viewport_index) |
                             S_02881C_USE_VTX_VRS_RATE(outinfo->writes_primitive_shading_rate) |
                             S_02881C_VS_OUT_MISC_VEC_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0f) != 0) |
                             S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xf0) != 0) |
                             total_mask << 8 | clip_dist_mask);

   radeon_set_context_reg(ctx_cs, R_028A84_VGT_PRIMITIVEID_EN,
                          S_028A84_PRIMITIVEID_EN(es_enable_prim_id) |
                             S_028A84_NGG_DISABLE_PROVOK_REUSE(outinfo->export_prim_id));

   radeon_set_context_reg(ctx_cs, R_028AAC_VGT_ESGS_RING_ITEMSIZE,
                          ngg_state->vgt_esgs_ring_itemsize);

   /* NGG specific registers. */
   struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];
   uint32_t gs_num_invocations = gs ? gs->info.gs.invocations : 1;

   if (pdevice->rad_info.gfx_level < GFX11) {
      radeon_set_context_reg(
         ctx_cs, R_028A44_VGT_GS_ONCHIP_CNTL,
         S_028A44_ES_VERTS_PER_SUBGRP(ngg_state->hw_max_esverts) |
            S_028A44_GS_PRIMS_PER_SUBGRP(ngg_state->max_gsprims) |
            S_028A44_GS_INST_PRIMS_IN_SUBGRP(ngg_state->max_gsprims * gs_num_invocations));
   }

   radeon_set_context_reg(ctx_cs, R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP,
                          S_0287FC_MAX_VERTS_PER_SUBGROUP(ngg_state->max_out_verts));
   radeon_set_context_reg(ctx_cs, R_028B4C_GE_NGG_SUBGRP_CNTL,
                          S_028B4C_PRIM_AMP_FACTOR(ngg_state->prim_amp_factor) |
                             S_028B4C_THDS_PER_SUBGRP(0)); /* for fast launch */
   radeon_set_context_reg(
      ctx_cs, R_028B90_VGT_GS_INSTANCE_CNT,
      S_028B90_CNT(gs_num_invocations) | S_028B90_ENABLE(gs_num_invocations > 1) |
         S_028B90_EN_MAX_VERT_OUT_PER_GS_INSTANCE(ngg_state->max_vert_out_per_gs_instance));

   if (pdevice->rad_info.gfx_level >= GFX11) {
      ge_cntl = S_03096C_PRIMS_PER_SUBGRP(ngg_state->max_gsprims) |
                S_03096C_VERTS_PER_SUBGRP(ngg_state->enable_vertex_grouping
                                          ? ngg_state->hw_max_esverts
                                          : 256) | /* 256 = disable vertex grouping */
                S_03096C_BREAK_PRIMGRP_AT_EOI(break_wave_at_eoi) |
                S_03096C_PRIM_GRP_SIZE_GFX11(256);
   } else {
      ge_cntl = S_03096C_PRIM_GRP_SIZE_GFX10(ngg_state->max_gsprims) |
                S_03096C_VERT_GRP_SIZE(ngg_state->enable_vertex_grouping
                                          ? ngg_state->hw_max_esverts
                                          : 256) | /* 256 = disable vertex grouping */
                S_03096C_BREAK_WAVE_AT_EOI(break_wave_at_eoi);
   }

   /* Bug workaround for a possible hang with non-tessellation cases.
    * Tessellation always sets GE_CNTL.VERT_GRP_SIZE = 0
    *
    * Requirement: GE_CNTL.VERT_GRP_SIZE = VGT_GS_ONCHIP_CNTL.ES_VERTS_PER_SUBGRP - 5
    */
   if (pdevice->rad_info.gfx_level == GFX10 &&
       !radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL) && ngg_state->hw_max_esverts != 256) {
      ge_cntl &= C_03096C_VERT_GRP_SIZE;

      if (ngg_state->hw_max_esverts > 5) {
         ge_cntl |= S_03096C_VERT_GRP_SIZE(ngg_state->hw_max_esverts - 5);
      }
   }

   radeon_set_uconfig_reg(ctx_cs, R_03096C_GE_CNTL, ge_cntl);

   unsigned late_alloc_wave64, cu_mask;
   ac_compute_late_alloc(&pdevice->rad_info, true, shader->info.has_ngg_culling,
                         shader->config.scratch_bytes_per_wave > 0, &late_alloc_wave64, &cu_mask);

   if (pdevice->rad_info.gfx_level >= GFX11) {
      /* TODO: figure out how S_00B204_CU_EN_GFX11 interacts with ac_set_reg_cu_en */
      gfx10_set_sh_reg_idx3(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
                            S_00B21C_CU_EN(cu_mask) | S_00B21C_WAVE_LIMIT(0x3F));
      gfx10_set_sh_reg_idx3(
         cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS,
         S_00B204_CU_EN_GFX11(0x1) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(late_alloc_wave64));
   } else if (pdevice->rad_info.gfx_level >= GFX10) {
      ac_set_reg_cu_en(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
                       S_00B21C_CU_EN(cu_mask) | S_00B21C_WAVE_LIMIT(0x3F),
                       C_00B21C_CU_EN, 0, &pdevice->rad_info, (void*)gfx10_set_sh_reg_idx3);
      ac_set_reg_cu_en(cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS,
                       S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(late_alloc_wave64),
                       C_00B204_CU_EN_GFX10, 16, &pdevice->rad_info,
                       (void*)gfx10_set_sh_reg_idx3);
   } else {
      radeon_set_sh_reg_idx(
         pdevice, cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS, 3,
         S_00B21C_CU_EN(cu_mask) | S_00B21C_WAVE_LIMIT(0x3F));
      radeon_set_sh_reg_idx(
         pdevice, cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS, 3,
         S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(late_alloc_wave64));
   }

   uint32_t oversub_pc_lines = late_alloc_wave64 ? pdevice->rad_info.pc_lines / 4 : 0;
   if (shader->info.has_ngg_culling) {
      unsigned oversub_factor = 2;

      if (outinfo->param_exports > 4)
         oversub_factor = 4;
      else if (outinfo->param_exports > 2)
         oversub_factor = 3;

      oversub_pc_lines *= oversub_factor;
   }

   gfx10_emit_ge_pc_alloc(cs, pdevice->rad_info.gfx_level, oversub_pc_lines);
}

static void
radv_pipeline_emit_hw_hs(struct radeon_cmdbuf *cs, const struct radv_graphics_pipeline *pipeline,
                         const struct radv_shader *shader)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint64_t va = radv_shader_get_va(shader);

   if (pdevice->rad_info.gfx_level >= GFX9) {
      if (pdevice->rad_info.gfx_level >= GFX10) {
         radeon_set_sh_reg(cs, R_00B520_SPI_SHADER_PGM_LO_LS, va >> 8);
      } else {
         radeon_set_sh_reg(cs, R_00B410_SPI_SHADER_PGM_LO_LS, va >> 8);
      }

      radeon_set_sh_reg(cs, R_00B428_SPI_SHADER_PGM_RSRC1_HS, shader->config.rsrc1);
   } else {
      radeon_set_sh_reg_seq(cs, R_00B420_SPI_SHADER_PGM_LO_HS, 4);
      radeon_emit(cs, va >> 8);
      radeon_emit(cs, S_00B424_MEM_BASE(va >> 40));
      radeon_emit(cs, shader->config.rsrc1);
      radeon_emit(cs, shader->config.rsrc2);
   }
}

static void
radv_pipeline_emit_vertex_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                 const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *vs;

   /* Skip shaders merged into HS/GS */
   vs = pipeline->base.shaders[MESA_SHADER_VERTEX];
   if (!vs)
      return;

   if (vs->info.vs.as_ls)
      radv_pipeline_emit_hw_ls(cs, pipeline, vs);
   else if (vs->info.vs.as_es)
      radv_pipeline_emit_hw_es(cs, pipeline, vs);
   else if (vs->info.is_ngg)
      radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, vs);
   else
      radv_pipeline_emit_hw_vs(ctx_cs, cs, pipeline, vs);
}

static void
radv_pipeline_emit_tess_shaders(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *tes, *tcs;

   tcs = pipeline->base.shaders[MESA_SHADER_TESS_CTRL];
   tes = pipeline->base.shaders[MESA_SHADER_TESS_EVAL];

   if (tes) {
      if (tes->info.is_ngg) {
         radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, tes);
      } else if (tes->info.tes.as_es)
         radv_pipeline_emit_hw_es(cs, pipeline, tes);
      else
         radv_pipeline_emit_hw_vs(ctx_cs, cs, pipeline, tes);
   }

   radv_pipeline_emit_hw_hs(cs, pipeline, tcs);

   if (pdevice->rad_info.gfx_level >= GFX10 &&
       !radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY) && !radv_pipeline_has_ngg(pipeline)) {
      radeon_set_context_reg(ctx_cs, R_028A44_VGT_GS_ONCHIP_CNTL,
                             S_028A44_ES_VERTS_PER_SUBGRP(250) | S_028A44_GS_PRIMS_PER_SUBGRP(126) |
                                S_028A44_GS_INST_PRIMS_IN_SUBGRP(126));
   }
}

static void
radv_pipeline_emit_tess_state(struct radeon_cmdbuf *ctx_cs,
                              const struct radv_graphics_pipeline *pipeline,
                              const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *tes = radv_get_shader(&pipeline->base, MESA_SHADER_TESS_EVAL);
   unsigned type = 0, partitioning = 0, topology = 0, distribution_mode = 0;

   switch (tes->info.tes._primitive_mode) {
   case TESS_PRIMITIVE_TRIANGLES:
      type = V_028B6C_TESS_TRIANGLE;
      break;
   case TESS_PRIMITIVE_QUADS:
      type = V_028B6C_TESS_QUAD;
      break;
   case TESS_PRIMITIVE_ISOLINES:
      type = V_028B6C_TESS_ISOLINE;
      break;
   default:
      break;
   }

   switch (tes->info.tes.spacing) {
   case TESS_SPACING_EQUAL:
      partitioning = V_028B6C_PART_INTEGER;
      break;
   case TESS_SPACING_FRACTIONAL_ODD:
      partitioning = V_028B6C_PART_FRAC_ODD;
      break;
   case TESS_SPACING_FRACTIONAL_EVEN:
      partitioning = V_028B6C_PART_FRAC_EVEN;
      break;
   default:
      break;
   }

   bool ccw = tes->info.tes.ccw;
   if (state->ts->domain_origin != VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT)
      ccw = !ccw;

   if (tes->info.tes.point_mode)
      topology = V_028B6C_OUTPUT_POINT;
   else if (tes->info.tes._primitive_mode == TESS_PRIMITIVE_ISOLINES)
      topology = V_028B6C_OUTPUT_LINE;
   else if (ccw)
      topology = V_028B6C_OUTPUT_TRIANGLE_CCW;
   else
      topology = V_028B6C_OUTPUT_TRIANGLE_CW;

   if (pdevice->rad_info.has_distributed_tess) {
      if (pdevice->rad_info.family == CHIP_FIJI || pdevice->rad_info.family >= CHIP_POLARIS10)
         distribution_mode = V_028B6C_TRAPEZOIDS;
      else
         distribution_mode = V_028B6C_DONUTS;
   } else
      distribution_mode = V_028B6C_NO_DIST;

   radeon_set_context_reg(ctx_cs, R_028B6C_VGT_TF_PARAM,
                          S_028B6C_TYPE(type) | S_028B6C_PARTITIONING(partitioning) |
                             S_028B6C_TOPOLOGY(topology) |
                             S_028B6C_DISTRIBUTION_MODE(distribution_mode));
}

static void
radv_pipeline_emit_hw_gs(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                         const struct radv_graphics_pipeline *pipeline, const struct radv_shader *gs)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct gfx9_gs_info *gs_state = &gs->info.gs_ring_info;
   unsigned gs_max_out_vertices;
   const uint8_t *num_components;
   uint8_t max_stream;
   unsigned offset;
   uint64_t va;

   gs_max_out_vertices = gs->info.gs.vertices_out;
   max_stream = gs->info.gs.max_stream;
   num_components = gs->info.gs.num_stream_output_components;

   offset = num_components[0] * gs_max_out_vertices;

   radeon_set_context_reg_seq(ctx_cs, R_028A60_VGT_GSVS_RING_OFFSET_1, 3);
   radeon_emit(ctx_cs, offset);
   if (max_stream >= 1)
      offset += num_components[1] * gs_max_out_vertices;
   radeon_emit(ctx_cs, offset);
   if (max_stream >= 2)
      offset += num_components[2] * gs_max_out_vertices;
   radeon_emit(ctx_cs, offset);
   if (max_stream >= 3)
      offset += num_components[3] * gs_max_out_vertices;
   radeon_set_context_reg(ctx_cs, R_028AB0_VGT_GSVS_RING_ITEMSIZE, offset);

   radeon_set_context_reg_seq(ctx_cs, R_028B5C_VGT_GS_VERT_ITEMSIZE, 4);
   radeon_emit(ctx_cs, num_components[0]);
   radeon_emit(ctx_cs, (max_stream >= 1) ? num_components[1] : 0);
   radeon_emit(ctx_cs, (max_stream >= 2) ? num_components[2] : 0);
   radeon_emit(ctx_cs, (max_stream >= 3) ? num_components[3] : 0);

   uint32_t gs_num_invocations = gs->info.gs.invocations;
   radeon_set_context_reg(
      ctx_cs, R_028B90_VGT_GS_INSTANCE_CNT,
      S_028B90_CNT(MIN2(gs_num_invocations, 127)) | S_028B90_ENABLE(gs_num_invocations > 0));

   radeon_set_context_reg(ctx_cs, R_028AAC_VGT_ESGS_RING_ITEMSIZE,
                          gs_state->vgt_esgs_ring_itemsize);

   va = radv_shader_get_va(gs);

   if (pdevice->rad_info.gfx_level >= GFX9) {
      if (pdevice->rad_info.gfx_level >= GFX10) {
         radeon_set_sh_reg(cs, R_00B320_SPI_SHADER_PGM_LO_ES, va >> 8);
      } else {
         radeon_set_sh_reg(cs, R_00B210_SPI_SHADER_PGM_LO_ES, va >> 8);
      }

      radeon_set_sh_reg_seq(cs, R_00B228_SPI_SHADER_PGM_RSRC1_GS, 2);
      radeon_emit(cs, gs->config.rsrc1);
      radeon_emit(cs, gs->config.rsrc2 | S_00B22C_LDS_SIZE(gs_state->lds_size));

      radeon_set_context_reg(ctx_cs, R_028A44_VGT_GS_ONCHIP_CNTL, gs_state->vgt_gs_onchip_cntl);
      radeon_set_context_reg(ctx_cs, R_028A94_VGT_GS_MAX_PRIMS_PER_SUBGROUP,
                             gs_state->vgt_gs_max_prims_per_subgroup);
   } else {
      radeon_set_sh_reg_seq(cs, R_00B220_SPI_SHADER_PGM_LO_GS, 4);
      radeon_emit(cs, va >> 8);
      radeon_emit(cs, S_00B224_MEM_BASE(va >> 40));
      radeon_emit(cs, gs->config.rsrc1);
      radeon_emit(cs, gs->config.rsrc2);
   }

   if (pdevice->rad_info.gfx_level >= GFX10) {
      ac_set_reg_cu_en(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
                       S_00B21C_CU_EN(0xffff) | S_00B21C_WAVE_LIMIT(0x3F),
                       C_00B21C_CU_EN, 0, &pdevice->rad_info,
                       (void*)gfx10_set_sh_reg_idx3);
      ac_set_reg_cu_en(cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS,
                       S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(0),
                       C_00B204_CU_EN_GFX10, 16, &pdevice->rad_info,
                       (void*)gfx10_set_sh_reg_idx3);
   } else if (pdevice->rad_info.gfx_level >= GFX7) {
      radeon_set_sh_reg_idx(
         pdevice, cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS, 3,
         S_00B21C_CU_EN(0xffff) | S_00B21C_WAVE_LIMIT(0x3F));

      if (pdevice->rad_info.gfx_level >= GFX10) {
         radeon_set_sh_reg_idx(
            pdevice, cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS, 3,
            S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(0));
      }
   }

   radv_pipeline_emit_hw_vs(ctx_cs, cs, pipeline, pipeline->base.gs_copy_shader);
}

static void
radv_pipeline_emit_geometry_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                   const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *gs;

   gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];
   if (!gs)
      return;

   if (gs->info.is_ngg)
      radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, gs);
   else
      radv_pipeline_emit_hw_gs(ctx_cs, cs, pipeline, gs);

   radeon_set_context_reg(ctx_cs, R_028B38_VGT_GS_MAX_VERT_OUT, gs->info.gs.vertices_out);
}

static void
radv_pipeline_emit_mesh_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                               const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *ms = pipeline->base.shaders[MESA_SHADER_MESH];
   if (!ms)
      return;

   radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, ms);
   radeon_set_context_reg(ctx_cs, R_028B38_VGT_GS_MAX_VERT_OUT, ms->info.workgroup_size);
   radeon_set_uconfig_reg_idx(pdevice, ctx_cs,
                              R_030908_VGT_PRIMITIVE_TYPE, 1, V_008958_DI_PT_POINTLIST);
}

static uint32_t
offset_to_ps_input(uint32_t offset, bool flat_shade, bool explicit, bool float16)
{
   uint32_t ps_input_cntl;
   if (offset <= AC_EXP_PARAM_OFFSET_31) {
      ps_input_cntl = S_028644_OFFSET(offset);
      if (flat_shade || explicit)
         ps_input_cntl |= S_028644_FLAT_SHADE(1);
      if (explicit) {
         /* Force parameter cache to be read in passthrough
          * mode.
          */
         ps_input_cntl |= S_028644_OFFSET(1 << 5);
      }
      if (float16) {
         ps_input_cntl |= S_028644_FP16_INTERP_MODE(1) | S_028644_ATTR0_VALID(1);
      }
   } else {
      /* The input is a DEFAULT_VAL constant. */
      assert(offset >= AC_EXP_PARAM_DEFAULT_VAL_0000 && offset <= AC_EXP_PARAM_DEFAULT_VAL_1111);
      offset -= AC_EXP_PARAM_DEFAULT_VAL_0000;
      ps_input_cntl = S_028644_OFFSET(0x20) | S_028644_DEFAULT_VAL(offset);
   }
   return ps_input_cntl;
}

static void
single_slot_to_ps_input(const struct radv_vs_output_info *outinfo,
                        unsigned slot, uint32_t *ps_input_cntl, unsigned *ps_offset,
                        bool skip_undef, bool use_default_0, bool flat_shade)
{
   unsigned vs_offset = outinfo->vs_output_param_offset[slot];

   if (vs_offset == AC_EXP_PARAM_UNDEFINED) {
      if (skip_undef)
         return;
      else if (use_default_0)
         vs_offset = AC_EXP_PARAM_DEFAULT_VAL_0000;
      else
         unreachable("vs_offset should not be AC_EXP_PARAM_UNDEFINED.");
   }

   ps_input_cntl[*ps_offset] = offset_to_ps_input(vs_offset, flat_shade, false, false);
   ++(*ps_offset);
}

static void
input_mask_to_ps_inputs(const struct radv_vs_output_info *outinfo, const struct radv_shader *ps,
                        uint32_t input_mask, uint32_t *ps_input_cntl, unsigned *ps_offset)
{
   u_foreach_bit(i, input_mask) {
      unsigned vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_VAR0 + i];
      if (vs_offset == AC_EXP_PARAM_UNDEFINED) {
         ps_input_cntl[*ps_offset] = S_028644_OFFSET(0x20);
         ++(*ps_offset);
         continue;
      }

      bool flat_shade = !!(ps->info.ps.flat_shaded_mask & (1u << *ps_offset));
      bool explicit = !!(ps->info.ps.explicit_shaded_mask & (1u << *ps_offset));
      bool float16 = !!(ps->info.ps.float16_shaded_mask & (1u << *ps_offset));

      ps_input_cntl[*ps_offset] = offset_to_ps_input(vs_offset, flat_shade, explicit, float16);
      ++(*ps_offset);
   }
}

static void
radv_pipeline_emit_ps_inputs(struct radeon_cmdbuf *ctx_cs,
                             const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   bool mesh = radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH);
   uint32_t ps_input_cntl[32];

   unsigned ps_offset = 0;

   if (ps->info.ps.prim_id_input && !mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_PRIMITIVE_ID, ps_input_cntl, &ps_offset,
                              true, false, true);

   if (ps->info.ps.layer_input && !mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_LAYER, ps_input_cntl, &ps_offset,
                              false, true, true);

   if (ps->info.ps.viewport_index_input && !mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_VIEWPORT, ps_input_cntl, &ps_offset,
                              false, false, true);

   if (ps->info.ps.has_pcoord)
      ps_input_cntl[ps_offset++] = S_028644_PT_SPRITE_TEX(1) | S_028644_OFFSET(0x20);

   if (ps->info.ps.num_input_clips_culls) {
      single_slot_to_ps_input(outinfo, VARYING_SLOT_CLIP_DIST0, ps_input_cntl, &ps_offset,
                              true, false, false);

      if (ps->info.ps.num_input_clips_culls > 4)
         single_slot_to_ps_input(outinfo, VARYING_SLOT_CLIP_DIST1, ps_input_cntl, &ps_offset,
                                 true, false, false);
   }

   input_mask_to_ps_inputs(outinfo, ps, ps->info.ps.input_mask,
                           ps_input_cntl, &ps_offset);

   /* Per-primitive PS inputs: the HW needs these to be last. */

   if (ps->info.ps.prim_id_input && mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_PRIMITIVE_ID, ps_input_cntl, &ps_offset,
                              true, false, false);

   if (ps->info.ps.layer_input && mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_LAYER, ps_input_cntl, &ps_offset,
                              false, true, false);

   if (ps->info.ps.viewport_index_input && mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_VIEWPORT, ps_input_cntl, &ps_offset,
                              false, false, false);

   input_mask_to_ps_inputs(outinfo, ps, ps->info.ps.input_per_primitive_mask,
                           ps_input_cntl, &ps_offset);

   if (ps_offset) {
      radeon_set_context_reg_seq(ctx_cs, R_028644_SPI_PS_INPUT_CNTL_0, ps_offset);
      for (unsigned i = 0; i < ps_offset; i++) {
         radeon_emit(ctx_cs, ps_input_cntl[i]);
      }
   }
}

static uint32_t
radv_compute_db_shader_control(const struct radv_physical_device *pdevice,
                               const struct radv_graphics_pipeline *pipeline,
                               const struct radv_shader *ps)
{
   unsigned conservative_z_export = V_02880C_EXPORT_ANY_Z;
   unsigned z_order;
   if (ps->info.ps.early_fragment_test || !ps->info.ps.writes_memory)
      z_order = V_02880C_EARLY_Z_THEN_LATE_Z;
   else
      z_order = V_02880C_LATE_Z;

   if (ps->info.ps.depth_layout == FRAG_DEPTH_LAYOUT_GREATER)
      conservative_z_export = V_02880C_EXPORT_GREATER_THAN_Z;
   else if (ps->info.ps.depth_layout == FRAG_DEPTH_LAYOUT_LESS)
      conservative_z_export = V_02880C_EXPORT_LESS_THAN_Z;

   bool disable_rbplus = pdevice->rad_info.has_rbplus && !pdevice->rad_info.rbplus_allowed;

   /* It shouldn't be needed to export gl_SampleMask when MSAA is disabled
    * but this appears to break Project Cars (DXVK). See
    * https://bugs.freedesktop.org/show_bug.cgi?id=109401
    */
   bool mask_export_enable = ps->info.ps.writes_sample_mask;

   return S_02880C_Z_EXPORT_ENABLE(ps->info.ps.writes_z) |
          S_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(ps->info.ps.writes_stencil) |
          S_02880C_KILL_ENABLE(!!ps->info.ps.can_discard) |
          S_02880C_MASK_EXPORT_ENABLE(mask_export_enable) |
          S_02880C_CONSERVATIVE_Z_EXPORT(conservative_z_export) | S_02880C_Z_ORDER(z_order) |
          S_02880C_DEPTH_BEFORE_SHADER(ps->info.ps.early_fragment_test) |
          S_02880C_PRE_SHADER_DEPTH_COVERAGE_ENABLE(ps->info.ps.post_depth_coverage) |
          S_02880C_EXEC_ON_HIER_FAIL(ps->info.ps.writes_memory) |
          S_02880C_EXEC_ON_NOOP(ps->info.ps.writes_memory) |
          S_02880C_DUAL_QUAD_DISABLE(disable_rbplus);
}

static void
radv_pipeline_emit_fragment_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                   const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *ps;
   bool param_gen;
   uint64_t va;
   assert(pipeline->base.shaders[MESA_SHADER_FRAGMENT]);

   ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   va = radv_shader_get_va(ps);

   radeon_set_sh_reg_seq(cs, R_00B020_SPI_SHADER_PGM_LO_PS, 4);
   radeon_emit(cs, va >> 8);
   radeon_emit(cs, S_00B024_MEM_BASE(va >> 40));
   radeon_emit(cs, ps->config.rsrc1);
   radeon_emit(cs, ps->config.rsrc2);

   radeon_set_context_reg(ctx_cs, R_02880C_DB_SHADER_CONTROL,
                          radv_compute_db_shader_control(pdevice, pipeline, ps));

   radeon_set_context_reg_seq(ctx_cs, R_0286CC_SPI_PS_INPUT_ENA, 2);
   radeon_emit(ctx_cs, ps->config.spi_ps_input_ena);
   radeon_emit(ctx_cs, ps->config.spi_ps_input_addr);

   /* Workaround when there are no PS inputs but LDS is used. */
   param_gen = pdevice->rad_info.gfx_level >= GFX11 &&
               !ps->info.ps.num_interp && ps->config.lds_size;

   radeon_set_context_reg(
      ctx_cs, R_0286D8_SPI_PS_IN_CONTROL,
      S_0286D8_NUM_INTERP(ps->info.ps.num_interp) |
      S_0286D8_NUM_PRIM_INTERP(ps->info.ps.num_prim_interp) |
      S_0286D8_PS_W32_EN(ps->info.wave_size == 32) |
      S_0286D8_PARAM_GEN(param_gen));

   radeon_set_context_reg(ctx_cs, R_0286E0_SPI_BARYC_CNTL, pipeline->spi_baryc_cntl);

   radeon_set_context_reg(
      ctx_cs, R_028710_SPI_SHADER_Z_FORMAT,
      ac_get_spi_shader_z_format(ps->info.ps.writes_z, ps->info.ps.writes_stencil,
                                 ps->info.ps.writes_sample_mask, false));
}

static void
radv_pipeline_emit_vgt_vertex_reuse(struct radeon_cmdbuf *ctx_cs,
                                    const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;

   if (pdevice->rad_info.family < CHIP_POLARIS10 || pdevice->rad_info.gfx_level >= GFX10)
      return;

   unsigned vtx_reuse_depth = 30;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL) &&
       radv_get_shader(&pipeline->base, MESA_SHADER_TESS_EVAL)->info.tes.spacing ==
          TESS_SPACING_FRACTIONAL_ODD) {
      vtx_reuse_depth = 14;
   }
   radeon_set_context_reg(ctx_cs, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL,
                          S_028C58_VTX_REUSE_DEPTH(vtx_reuse_depth));
}

static void
radv_pipeline_emit_vgt_shader_config(struct radeon_cmdbuf *ctx_cs,
                                     const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint32_t stages = 0;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      stages |= S_028B54_LS_EN(V_028B54_LS_STAGE_ON) | S_028B54_HS_EN(1) | S_028B54_DYNAMIC_HS(1);

      if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
         stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_DS) | S_028B54_GS_EN(1);
      else if (radv_pipeline_has_ngg(pipeline))
         stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_DS);
      else
         stages |= S_028B54_VS_EN(V_028B54_VS_STAGE_DS);
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_REAL) | S_028B54_GS_EN(1);
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH)) {
      assert(!radv_pipeline_has_ngg_passthrough(pipeline));
      stages |= S_028B54_GS_EN(1) | S_028B54_GS_FAST_LAUNCH(1);

      if (pipeline->base.shaders[MESA_SHADER_MESH]->info.ms.needs_ms_scratch_ring)
         stages |= S_028B54_NGG_WAVE_ID_EN(1);
   } else if (radv_pipeline_has_ngg(pipeline)) {
      stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_REAL);
   }

   if (radv_pipeline_has_ngg(pipeline)) {
      stages |= S_028B54_PRIMGEN_EN(1);
      if (pipeline->streamout_shader)
         stages |= S_028B54_NGG_WAVE_ID_EN(1);
      if (radv_pipeline_has_ngg_passthrough(pipeline)) {
         stages |= S_028B54_PRIMGEN_PASSTHRU_EN(1);
         if (pdevice->rad_info.family >= CHIP_NAVI23)
            stages |= S_028B54_PRIMGEN_PASSTHRU_NO_MSG(1);
      }
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      stages |= S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);
   }

   if (pdevice->rad_info.gfx_level >= GFX9)
      stages |= S_028B54_MAX_PRIMGRP_IN_WAVE(2);

   if (pdevice->rad_info.gfx_level >= GFX10) {
      uint8_t hs_size = 64, gs_size = 64, vs_size = 64;

      if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL))
         hs_size = pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.wave_size;

      if (pipeline->base.shaders[MESA_SHADER_GEOMETRY]) {
         vs_size = gs_size = pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.wave_size;
         if (radv_pipeline_has_gs_copy_shader(&pipeline->base))
            vs_size = pipeline->base.gs_copy_shader->info.wave_size;
      } else if (pipeline->base.shaders[MESA_SHADER_TESS_EVAL])
         vs_size = pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.wave_size;
      else if (pipeline->base.shaders[MESA_SHADER_VERTEX])
         vs_size = pipeline->base.shaders[MESA_SHADER_VERTEX]->info.wave_size;
      else if (pipeline->base.shaders[MESA_SHADER_MESH])
         vs_size = gs_size = pipeline->base.shaders[MESA_SHADER_MESH]->info.wave_size;

      if (radv_pipeline_has_ngg(pipeline)) {
         assert(!radv_pipeline_has_gs_copy_shader(&pipeline->base));
         gs_size = vs_size;
      }

      /* legacy GS only supports Wave64 */
      stages |= S_028B54_HS_W32_EN(hs_size == 32 ? 1 : 0) |
                S_028B54_GS_W32_EN(gs_size == 32 ? 1 : 0) |
                S_028B54_VS_W32_EN(vs_size == 32 ? 1 : 0);
   }

   radeon_set_context_reg(ctx_cs, R_028B54_VGT_SHADER_STAGES_EN, stages);
}

static void
radv_pipeline_emit_cliprect_rule(struct radeon_cmdbuf *ctx_cs,
                                 const struct vk_graphics_pipeline_state *state)
{
   uint32_t cliprect_rule = 0;

   if (!state->dr->rectangle_count) {
      cliprect_rule = 0xffff;
   } else {
      for (unsigned i = 0; i < (1u << MAX_DISCARD_RECTANGLES); ++i) {
         /* Interpret i as a bitmask, and then set the bit in
          * the mask if that combination of rectangles in which
          * the pixel is contained should pass the cliprect
          * test.
          */
         unsigned relevant_subset = i & ((1u << state->dr->rectangle_count) - 1);

         if (state->dr->mode == VK_DISCARD_RECTANGLE_MODE_INCLUSIVE_EXT && !relevant_subset)
            continue;

         if (state->dr->mode == VK_DISCARD_RECTANGLE_MODE_EXCLUSIVE_EXT && relevant_subset)
            continue;

         cliprect_rule |= 1u << i;
      }
   }

   radeon_set_context_reg(ctx_cs, R_02820C_PA_SC_CLIPRECT_RULE, cliprect_rule);
}

static void
radv_pipeline_emit_vgt_gs_out(struct radeon_cmdbuf *ctx_cs,
                              const struct radv_graphics_pipeline *pipeline,
                              uint32_t vgt_gs_out_prim_type)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;

   if (pdevice->rad_info.gfx_level >= GFX11) {
      radeon_set_uconfig_reg(ctx_cs, R_030998_VGT_GS_OUT_PRIM_TYPE, vgt_gs_out_prim_type);
   } else {
      radeon_set_context_reg(ctx_cs, R_028A6C_VGT_GS_OUT_PRIM_TYPE, vgt_gs_out_prim_type);
   }
}

static void
gfx103_pipeline_emit_vgt_draw_payload_cntl(struct radeon_cmdbuf *ctx_cs,
                                           const struct radv_graphics_pipeline *pipeline,
                                           const struct vk_graphics_pipeline_state *state)
{
   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);

   bool enable_vrs = radv_is_vrs_enabled(pipeline, state);

   /* Enables the second channel of the primitive export instruction.
    * This channel contains: VRS rate x, y, viewport and layer.
    */
   bool enable_prim_payload =
      outinfo &&
      (outinfo->writes_viewport_index_per_primitive ||
       outinfo->writes_layer_per_primitive ||
       outinfo->writes_primitive_shading_rate_per_primitive);

   radeon_set_context_reg(ctx_cs, R_028A98_VGT_DRAW_PAYLOAD_CNTL,
                          S_028A98_EN_VRS_RATE(enable_vrs) |
                          S_028A98_EN_PRIM_PAYLOAD(enable_prim_payload));
}

static bool
gfx103_pipeline_vrs_coarse_shading(const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   struct radv_device *device = pipeline->base.device;

   if (device->instance->debug_flags & RADV_DEBUG_NO_VRS_FLAT_SHADING)
      return false;

   if (!ps->info.ps.allow_flat_shading)
      return false;

   return true;
}

static void
gfx103_pipeline_emit_vrs_state(struct radeon_cmdbuf *ctx_cs,
                               const struct radv_graphics_pipeline *pipeline,
                               const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint32_t mode = V_028064_VRS_COMB_MODE_PASSTHRU;
   uint8_t rate_x = 0, rate_y = 0;
   bool enable_vrs = radv_is_vrs_enabled(pipeline, state);

   if (!enable_vrs && gfx103_pipeline_vrs_coarse_shading(pipeline)) {
      /* When per-draw VRS is not enabled at all, try enabling VRS coarse shading 2x2 if the driver
       * determined that it's safe to enable.
       */
      mode = V_028064_VRS_COMB_MODE_OVERRIDE;
      rate_x = rate_y = 1;
   } else if (!radv_is_static_vrs_enabled(pipeline, state) && pipeline->force_vrs_per_vertex &&
              get_vs_output_info(pipeline)->writes_primitive_shading_rate) {
      /* Otherwise, if per-draw VRS is not enabled statically, try forcing per-vertex VRS if
       * requested by the user. Note that vkd3d-proton always has to declare VRS as dynamic because
       * in DX12 it's fully dynamic.
       */
      radeon_set_context_reg(ctx_cs, R_028848_PA_CL_VRS_CNTL,
         S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE) |
         S_028848_VERTEX_RATE_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE));

      /* If the shader is using discard, turn off coarse shading because discard at 2x2 pixel
       * granularity degrades quality too much. MIN allows sample shading but not coarse shading.
       */
      struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];

      mode = ps->info.ps.can_discard ? V_028064_VRS_COMB_MODE_MIN : V_028064_VRS_COMB_MODE_PASSTHRU;
   }

   if (pdevice->rad_info.gfx_level >= GFX11) {
      radeon_set_context_reg(ctx_cs, R_0283D0_PA_SC_VRS_OVERRIDE_CNTL,
                             S_0283D0_VRS_OVERRIDE_RATE_COMBINER_MODE(mode) |
                                S_0283D0_VRS_RATE((rate_x << 2) | rate_y));
   } else {
      radeon_set_context_reg(ctx_cs, R_028064_DB_VRS_OVERRIDE_CNTL,
                             S_028064_VRS_OVERRIDE_RATE_COMBINER_MODE(mode) |
                                S_028064_VRS_OVERRIDE_RATE_X(rate_x) |
                                S_028064_VRS_OVERRIDE_RATE_Y(rate_y));
   }
}

static void
radv_pipeline_emit_pm4(struct radv_graphics_pipeline *pipeline,
                       const struct radv_blend_state *blend,
                       const struct radv_depth_stencil_state *ds_state,
                       uint32_t vgt_gs_out_prim_type,
                       const struct vk_graphics_pipeline_state *state)

{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radeon_cmdbuf *ctx_cs = &pipeline->base.ctx_cs;
   struct radeon_cmdbuf *cs = &pipeline->base.cs;

   cs->max_dw = 64;
   ctx_cs->max_dw = 256;
   cs->buf = malloc(4 * (cs->max_dw + ctx_cs->max_dw));
   ctx_cs->buf = cs->buf + cs->max_dw;

   radv_pipeline_emit_depth_stencil_state(ctx_cs, ds_state);
   radv_pipeline_emit_blend_state(ctx_cs, pipeline, blend);
   radv_pipeline_emit_raster_state(ctx_cs, pipeline, state);
   radv_pipeline_emit_multisample_state(ctx_cs, pipeline);
   radv_pipeline_emit_vgt_gs_mode(ctx_cs, pipeline);
   radv_pipeline_emit_vertex_shader(ctx_cs, cs, pipeline);
   radv_pipeline_emit_mesh_shader(ctx_cs, cs, pipeline);

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      radv_pipeline_emit_tess_shaders(ctx_cs, cs, pipeline);
      radv_pipeline_emit_tess_state(ctx_cs, pipeline, state);
   }

   radv_pipeline_emit_geometry_shader(ctx_cs, cs, pipeline);
   radv_pipeline_emit_fragment_shader(ctx_cs, cs, pipeline);
   radv_pipeline_emit_ps_inputs(ctx_cs, pipeline);
   radv_pipeline_emit_vgt_vertex_reuse(ctx_cs, pipeline);
   radv_pipeline_emit_vgt_shader_config(ctx_cs, pipeline);
   radv_pipeline_emit_cliprect_rule(ctx_cs, state);
   radv_pipeline_emit_vgt_gs_out(ctx_cs, pipeline, vgt_gs_out_prim_type);

   if (pdevice->rad_info.gfx_level >= GFX10_3) {
      gfx103_pipeline_emit_vgt_draw_payload_cntl(ctx_cs, pipeline, state);
      gfx103_pipeline_emit_vrs_state(ctx_cs, pipeline, state);
   }

   pipeline->base.ctx_cs_hash = _mesa_hash_data(ctx_cs->buf, ctx_cs->cdw * 4);

   assert(ctx_cs->cdw <= ctx_cs->max_dw);
   assert(cs->cdw <= cs->max_dw);
}

static void
radv_pipeline_init_vertex_input_state(struct radv_graphics_pipeline *pipeline,
                                      const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct radv_shader_info *vs_info = &radv_get_shader(&pipeline->base, MESA_SHADER_VERTEX)->info;

   if (state->vi) {
      u_foreach_bit(i, state->vi->attributes_valid) {
         uint32_t binding = state->vi->attributes[i].binding;
         uint32_t offset = state->vi->attributes[i].offset;
         VkFormat format = state->vi->attributes[i].format;

         pipeline->attrib_ends[i] = offset + vk_format_get_blocksize(format);
         pipeline->attrib_bindings[i] = binding;

         if (state->vi->bindings[binding].stride) {
            pipeline->attrib_index_offset[i] = offset / state->vi->bindings[binding].stride;
         }
      }

      u_foreach_bit(i, state->vi->bindings_valid) {
         pipeline->binding_stride[i] = state->vi->bindings[i].stride;
      }
   }

   pipeline->use_per_attribute_vb_descs = vs_info->vs.use_per_attribute_vb_descs;
   pipeline->last_vertex_attrib_bit = util_last_bit(vs_info->vs.vb_desc_usage_mask);
   if (pipeline->base.shaders[MESA_SHADER_VERTEX])
      pipeline->next_vertex_stage = MESA_SHADER_VERTEX;
   else if (pipeline->base.shaders[MESA_SHADER_TESS_CTRL])
      pipeline->next_vertex_stage = MESA_SHADER_TESS_CTRL;
   else
      pipeline->next_vertex_stage = MESA_SHADER_GEOMETRY;
   if (pipeline->next_vertex_stage == MESA_SHADER_VERTEX) {
      const struct radv_shader *vs_shader = pipeline->base.shaders[MESA_SHADER_VERTEX];
      pipeline->can_use_simple_input = vs_shader->info.is_ngg == pdevice->use_ngg &&
                                       vs_shader->info.wave_size == pdevice->ge_wave_size;
   } else {
      pipeline->can_use_simple_input = false;
   }
   if (vs_info->vs.dynamic_inputs)
      pipeline->vb_desc_usage_mask = BITFIELD_MASK(pipeline->last_vertex_attrib_bit);
   else
      pipeline->vb_desc_usage_mask = vs_info->vs.vb_desc_usage_mask;
   pipeline->vb_desc_alloc_size = util_bitcount(pipeline->vb_desc_usage_mask) * 16;

   /* Prepare the VS input state for prologs created inside a library. */
   if (vs_info->vs.has_prolog && !(pipeline->dynamic_states & RADV_DYNAMIC_VERTEX_INPUT)) {
      const enum amd_gfx_level gfx_level = pdevice->rad_info.gfx_level;
      const enum radeon_family family = pdevice->rad_info.family;
      const struct ac_vtx_format_info *vtx_info_table = ac_get_vtx_format_info_table(gfx_level, family);

      pipeline->vs_input_state.bindings_match_attrib = true;

      u_foreach_bit(i, state->vi->attributes_valid) {
         uint32_t binding = state->vi->attributes[i].binding;
         uint32_t offset = state->vi->attributes[i].offset;

         pipeline->vs_input_state.bindings[i] = binding;
         pipeline->vs_input_state.bindings_match_attrib &= binding == i;

         if (state->vi->bindings[binding].input_rate) {
            pipeline->vs_input_state.instance_rate_inputs |= BITFIELD_BIT(i);
            pipeline->vs_input_state.divisors[i] = state->vi->bindings[binding].divisor;

            if (state->vi->bindings[binding].divisor == 0) {
               pipeline->vs_input_state.zero_divisors |= BITFIELD_BIT(i);
            } else if (state->vi->bindings[binding].divisor > 1) {
               pipeline->vs_input_state.nontrivial_divisors |= BITFIELD_BIT(i);
            }
         }

         pipeline->vs_input_state.offsets[i] = offset;

         enum pipe_format format = vk_format_to_pipe_format(state->vi->attributes[i].format);
         const struct ac_vtx_format_info *vtx_info = &vtx_info_table[format];

         pipeline->vs_input_state.formats[i] = format;
         uint8_t align_req_minus_1 = vtx_info->chan_byte_size >= 4 ? 3 : (vtx_info->element_size - 1);
         pipeline->vs_input_state.format_align_req_minus_1[i] = align_req_minus_1;
         pipeline->vs_input_state.format_sizes[i] = vtx_info->element_size;
         pipeline->vs_input_state.alpha_adjust_lo |= (vtx_info->alpha_adjust & 0x1) << i;
         pipeline->vs_input_state.alpha_adjust_hi |= (vtx_info->alpha_adjust >> 1) << i;
         if (G_008F0C_DST_SEL_X(vtx_info->dst_sel) == V_008F0C_SQ_SEL_Z) {
            pipeline->vs_input_state.post_shuffle |= BITFIELD_BIT(i);
         }

         if (!(vtx_info->has_hw_format & BITFIELD_BIT(vtx_info->num_channels - 1))) {
            pipeline->vs_input_state.nontrivial_formats |= BITFIELD_BIT(i);
         }
      }
   }
}

static struct radv_shader *
radv_pipeline_get_streamout_shader(struct radv_graphics_pipeline *pipeline)
{
   int i;

   for (i = MESA_SHADER_GEOMETRY; i >= MESA_SHADER_VERTEX; i--) {
      struct radv_shader *shader = radv_get_shader(&pipeline->base, i);

      if (shader && shader->info.so.num_outputs > 0)
         return shader;
   }

   return NULL;
}

static bool
radv_shader_need_indirect_descriptor_sets(struct radv_pipeline *pipeline, gl_shader_stage stage)
{
   struct radv_userdata_info *loc =
      radv_lookup_user_sgpr(pipeline, stage, AC_UD_INDIRECT_DESCRIPTOR_SETS);
   return loc->sgpr_idx != -1;
}

static void
radv_pipeline_init_shader_stages_state(struct radv_graphics_pipeline *pipeline)
{
   struct radv_device *device = pipeline->base.device;

   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      bool shader_exists = !!pipeline->base.shaders[i];
      if (shader_exists || i < MESA_SHADER_COMPUTE) {
         /* We need this info for some stages even when the shader doesn't exist. */
         pipeline->base.user_data_0[i] = radv_pipeline_stage_to_user_data_0(
            pipeline, i, device->physical_device->rad_info.gfx_level);

         if (shader_exists)
            pipeline->base.need_indirect_descriptor_sets |=
               radv_shader_need_indirect_descriptor_sets(&pipeline->base, i);
      }
   }

   gl_shader_stage first_stage =
      radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH) ? MESA_SHADER_MESH : MESA_SHADER_VERTEX;

   struct radv_userdata_info *loc =
      radv_lookup_user_sgpr(&pipeline->base, first_stage, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   if (loc->sgpr_idx != -1) {
      pipeline->vtx_base_sgpr = pipeline->base.user_data_0[first_stage];
      pipeline->vtx_base_sgpr += loc->sgpr_idx * 4;
      pipeline->vtx_emit_num = loc->num_sgprs;
      pipeline->uses_drawid =
         radv_get_shader(&pipeline->base, first_stage)->info.vs.needs_draw_id;
      pipeline->uses_baseinstance =
         radv_get_shader(&pipeline->base, first_stage)->info.vs.needs_base_instance;

      assert(first_stage != MESA_SHADER_MESH || !pipeline->uses_baseinstance);
   }
}

static uint32_t
radv_pipeline_init_vgt_gs_out(struct radv_graphics_pipeline *pipeline,
                              const struct vk_graphics_pipeline_state *state)
{
   uint32_t gs_out;

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      gs_out =
         si_conv_gl_prim_to_gs_out(pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.gs.output_prim);
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      if (pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.tes.point_mode) {
         gs_out = V_028A6C_POINTLIST;
      } else {
         gs_out = si_conv_tess_prim_to_gs_out(
            pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.tes._primitive_mode);
      }
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH)) {
      gs_out =
         si_conv_gl_prim_to_gs_out(pipeline->base.shaders[MESA_SHADER_MESH]->info.ms.output_prim);
   } else {
      gs_out = si_conv_prim_to_gs_out(si_translate_prim(state->ia->primitive_topology));
   }

   return gs_out;
}

static void
radv_pipeline_init_extra(struct radv_graphics_pipeline *pipeline,
                         const struct radv_graphics_pipeline_create_info *extra,
                         struct radv_blend_state *blend_state,
                         struct radv_depth_stencil_state *ds_state,
                         const struct vk_graphics_pipeline_state *state,
                         uint32_t *vgt_gs_out_prim_type)
{
   if (extra->custom_blend_mode == V_028808_CB_ELIMINATE_FAST_CLEAR ||
       extra->custom_blend_mode == V_028808_CB_FMASK_DECOMPRESS ||
       extra->custom_blend_mode == V_028808_CB_DCC_DECOMPRESS_GFX8 ||
       extra->custom_blend_mode == V_028808_CB_DCC_DECOMPRESS_GFX11 ||
       extra->custom_blend_mode == V_028808_CB_RESOLVE) {
      /* According to the CB spec states, CB_SHADER_MASK should be set to enable writes to all four
       * channels of MRT0.
       */
      blend_state->cb_shader_mask = 0xf;

      if (extra->custom_blend_mode == V_028808_CB_RESOLVE)
         pipeline->cb_color_control |= S_028808_DISABLE_DUAL_QUAD(1);

      pipeline->cb_color_control &= C_028808_MODE;
      pipeline->cb_color_control |= S_028808_MODE(extra->custom_blend_mode);
   }

   if (extra->use_rectlist) {
      struct radv_dynamic_state *dynamic = &pipeline->dynamic_state;
      dynamic->primitive_topology = V_008958_DI_PT_RECTLIST;

      *vgt_gs_out_prim_type = V_028A6C_TRISTRIP;
      if (radv_pipeline_has_ngg(pipeline))
         *vgt_gs_out_prim_type = V_028A6C_RECTLIST;

      pipeline->rast_prim = *vgt_gs_out_prim_type;
   }

   if (radv_pipeline_has_ds_attachments(state->rp)) {
      ds_state->db_render_control |= S_028000_DEPTH_CLEAR_ENABLE(extra->db_depth_clear);
      ds_state->db_render_control |= S_028000_STENCIL_CLEAR_ENABLE(extra->db_stencil_clear);
      ds_state->db_render_control |= S_028000_RESUMMARIZE_ENABLE(extra->resummarize_enable);
      ds_state->db_render_control |= S_028000_DEPTH_COMPRESS_DISABLE(extra->depth_compress_disable);
      ds_state->db_render_control |= S_028000_STENCIL_COMPRESS_DISABLE(extra->stencil_compress_disable);
   }
}

void
radv_pipeline_init(struct radv_device *device, struct radv_pipeline *pipeline,
                    enum radv_pipeline_type type)
{
   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);

   pipeline->device = device;
   pipeline->type = type;
}

static VkResult
radv_graphics_pipeline_init(struct radv_graphics_pipeline *pipeline, struct radv_device *device,
                            struct radv_pipeline_cache *cache,
                            const VkGraphicsPipelineCreateInfo *pCreateInfo,
                            const struct radv_graphics_pipeline_create_info *extra)
{
   struct radv_pipeline_layout pipeline_layout = {0};
   struct vk_graphics_pipeline_state state = {0};
   VkResult result;

   pipeline->last_vgt_api_stage = MESA_SHADER_NONE;

   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);
   VkGraphicsPipelineLibraryFlagBitsEXT imported_flags = 0;

   radv_pipeline_layout_init(device, &pipeline_layout, false);

   /* If we have libraries, import them first. */
   if (libs_info) {
      ASSERTED const bool link_optimize =
         (pCreateInfo->flags & VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT) != 0;

      for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
         struct radv_graphics_lib_pipeline *gfx_pipeline_lib =
            radv_pipeline_to_graphics_lib(pipeline_lib);

         assert(pipeline_lib->type == RADV_PIPELINE_GRAPHICS_LIB);

         /* If we have link time optimization, all libraries must be created with
          * VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT.
          */
         assert(!link_optimize || gfx_pipeline_lib->base.base.retain_shaders);

         radv_graphics_pipeline_import_lib(pipeline, &state, &pipeline_layout, gfx_pipeline_lib);

         imported_flags |= gfx_pipeline_lib->lib_flags;
      }
   }

   /* Import graphics pipeline info that was not included in the libraries. */
   result = radv_pipeline_import_graphics_info(pipeline, &state, &pipeline_layout, pCreateInfo,
                                               (~imported_flags) & ALL_GRAPHICS_LIB_FLAGS);
   if (result != VK_SUCCESS) {
      radv_pipeline_layout_finish(device, &pipeline_layout);
      return result;
   }

   radv_pipeline_layout_hash(&pipeline_layout);

   struct radv_blend_state blend = radv_pipeline_init_blend_state(pipeline, &state);

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   struct radv_pipeline_key key =
      radv_generate_graphics_pipeline_key(pipeline, pCreateInfo, &state, &blend);

   result = radv_create_shaders(&pipeline->base, &pipeline_layout, device, cache, &key, pCreateInfo->pStages,
                                pCreateInfo->stageCount, pCreateInfo->flags, NULL,
                                creation_feedback, NULL, NULL, &pipeline->last_vgt_api_stage);
   if (result != VK_SUCCESS) {
      radv_pipeline_layout_finish(device, &pipeline_layout);
      return result;
   }

   pipeline->spi_baryc_cntl = S_0286E0_FRONT_FACE_ALL_BITS(1);

   uint32_t vgt_gs_out_prim_type = radv_pipeline_init_vgt_gs_out(pipeline, &state);

   radv_pipeline_init_multisample_state(pipeline, &blend, &state, vgt_gs_out_prim_type);

   if (!radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      radv_pipeline_init_input_assembly_state(pipeline);
   radv_pipeline_init_dynamic_state(pipeline, &state);

   if (state.vp)
      pipeline->negative_one_to_one = state.vp->negative_one_to_one;

   radv_pipeline_init_raster_state(pipeline, &state);

   struct radv_depth_stencil_state ds_state =
      radv_pipeline_init_depth_stencil_state(pipeline, &state);

   if (device->physical_device->rad_info.gfx_level >= GFX10_3)
      gfx103_pipeline_init_vrs_state(pipeline, &state);

   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   blend.spi_shader_col_format = radv_compact_spi_shader_col_format(ps, &blend);

   /* Ensure that some export memory is always allocated, for two reasons:
    *
    * 1) Correctness: The hardware ignores the EXEC mask if no export
    *    memory is allocated, so KILL and alpha test do not work correctly
    *    without this.
    * 2) Performance: Every shader needs at least a NULL export, even when
    *    it writes no color/depth output. The NULL export instruction
    *    stalls without this setting.
    *
    * Don't add this to CB_SHADER_MASK.
    *
    * GFX10 supports pixel shaders without exports by setting both the
    * color and Z formats to SPI_SHADER_ZERO. The hw will skip export
    * instructions if any are present.
    */
   if ((device->physical_device->rad_info.gfx_level <= GFX9 || ps->info.ps.can_discard) &&
       !blend.spi_shader_col_format) {
      if (!ps->info.ps.writes_z && !ps->info.ps.writes_stencil && !ps->info.ps.writes_sample_mask)
         blend.spi_shader_col_format = V_028714_SPI_SHADER_32_R;
   }

   /* In presense of MRT holes (ie. the FS exports MRT1 but not MRT0), the compiler will remap them,
    * so that only MRT0 is exported and the driver will compact SPI_SHADER_COL_FORMAT to match what
    * the FS actually exports. Though, to make sure the hw remapping works as expected, we should
    * also clear color attachments without exports in CB_SHADER_MASK.
    */
   blend.cb_shader_mask &= ps->info.ps.colors_written;

   pipeline->col_format = blend.spi_shader_col_format;
   pipeline->cb_target_mask = blend.cb_target_mask;

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY) && !radv_pipeline_has_ngg(pipeline)) {
      struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];

      radv_pipeline_init_gs_ring_state(pipeline, &gs->info.gs_ring_info);
   }

   if (!radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      radv_pipeline_init_vertex_input_state(pipeline, &state);

   radv_pipeline_init_binning_state(pipeline, &blend, &state);
   radv_pipeline_init_shader_stages_state(pipeline);
   radv_pipeline_init_scratch(device, &pipeline->base);

   /* Find the last vertex shader stage that eventually uses streamout. */
   pipeline->streamout_shader = radv_pipeline_get_streamout_shader(pipeline);

   pipeline->is_ngg = radv_pipeline_has_ngg(pipeline);
   pipeline->has_ngg_culling =
      pipeline->is_ngg &&
      pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.has_ngg_culling;
   pipeline->force_vrs_per_vertex =
      pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.force_vrs_per_vertex;
   pipeline->uses_user_sample_locations = state.ms && state.ms->sample_locations_enable;
   pipeline->rast_prim = vgt_gs_out_prim_type;

   pipeline->base.push_constant_size = pipeline_layout.push_constant_size;
   pipeline->base.dynamic_offset_count = pipeline_layout.dynamic_offset_count;

   if (extra) {
      radv_pipeline_init_extra(pipeline, extra, &blend, &ds_state, &state, &vgt_gs_out_prim_type);
   }

   radv_pipeline_emit_pm4(pipeline, &blend, &ds_state, vgt_gs_out_prim_type, &state);

   radv_pipeline_layout_finish(device, &pipeline_layout);
   return result;
}

VkResult
radv_graphics_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                              const VkGraphicsPipelineCreateInfo *pCreateInfo,
                              const struct radv_graphics_pipeline_create_info *extra,
                              const VkAllocationCallbacks *pAllocator,
                              VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
   struct radv_graphics_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_GRAPHICS);

   result = radv_graphics_pipeline_init(pipeline, device, cache, pCreateInfo, extra);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base, pAllocator);
      return result;
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);

   return VK_SUCCESS;
}

static VkResult
radv_graphics_lib_pipeline_init(struct radv_graphics_lib_pipeline *pipeline,
                                struct radv_device *device, struct radv_pipeline_cache *cache,
                                const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   VkResult result;

   const VkGraphicsPipelineLibraryCreateInfoEXT *lib_info =
      vk_find_struct_const(pCreateInfo->pNext, GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT);
   const VkGraphicsPipelineLibraryFlagBitsEXT lib_flags = lib_info ? lib_info->flags : 0;
   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);
   VkGraphicsPipelineLibraryFlagBitsEXT imported_flags = lib_flags;

   struct vk_graphics_pipeline_state *state = &pipeline->graphics_state;
   struct radv_pipeline_layout *pipeline_layout = &pipeline->layout;

   pipeline->base.last_vgt_api_stage = MESA_SHADER_NONE;
   pipeline->base.base.retain_shaders =
      (pCreateInfo->flags & VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT) != 0;
   pipeline->lib_flags = lib_flags;

   radv_pipeline_layout_init(device, pipeline_layout, false);

   /* If we have libraries, import them first. */
   if (libs_info) {
      for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
         struct radv_graphics_lib_pipeline *gfx_pipeline_lib =
            radv_pipeline_to_graphics_lib(pipeline_lib);

         radv_graphics_pipeline_import_lib(&pipeline->base, state, pipeline_layout,
                                           gfx_pipeline_lib);

         pipeline->lib_flags |= gfx_pipeline_lib->lib_flags;

         imported_flags &= ~gfx_pipeline_lib->lib_flags;
      }
   }

   result = radv_pipeline_import_graphics_info(&pipeline->base, state, pipeline_layout, pCreateInfo,
                                               imported_flags);
   if (result != VK_SUCCESS)
      goto fail_layout;

   radv_pipeline_layout_hash(pipeline_layout);

   /* Compile a PS epilog if the fragment shader output interface is present without the main
    * fragment shader.
    */
   if ((imported_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) &&
       !(imported_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)) {
      struct radv_blend_state blend =
         radv_pipeline_init_blend_state(&pipeline->base, state);

      struct radv_pipeline_key key =
         radv_generate_graphics_pipeline_key(&pipeline->base, pCreateInfo, state, &blend);

      struct radv_ps_epilog_key epilog_key = {
         .spi_shader_col_format = blend.spi_shader_col_format,
         .color_is_int8 = blend.col_format_is_int8,
         .color_is_int10 = blend.col_format_is_int10,
         .enable_mrt_output_nan_fixup = key.ps.enable_mrt_output_nan_fixup,
      };

      pipeline->base.ps_epilog = radv_create_ps_epilog(device, &epilog_key);
      if (!pipeline->base.ps_epilog)
         goto fail_layout;
   }

   if (pipeline->base.active_stages != 0) {
      const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
         vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

      struct radv_blend_state blend =
         radv_pipeline_init_blend_state(&pipeline->base, state);

      struct radv_pipeline_key key =
         radv_generate_graphics_pipeline_key(&pipeline->base, pCreateInfo, state, &blend);

      /* FIXME: Force the driver to always retain the NIR shaders (after SPIRV->NIR) because it
       * doesn't yet support VS prologs and PS epilogs. This is very suboptimal, slow but for good
       * enough for a start.
       */
      VkPipelineCreateFlags flags =
         pCreateInfo->flags | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;

      result = radv_create_shaders(&pipeline->base.base, pipeline_layout, device, cache, &key,
                                   pCreateInfo->pStages, pCreateInfo->stageCount, flags, NULL,
                                   creation_feedback, NULL, NULL,
                                   &pipeline->base.last_vgt_api_stage);
      if (result != VK_SUCCESS && result != VK_PIPELINE_COMPILE_REQUIRED)
         goto fail_shaders;
   }

   return VK_SUCCESS;

fail_shaders:
   if (pipeline->base.ps_epilog)
      radv_shader_part_unref(device, pipeline->base.ps_epilog);
fail_layout:
   radv_pipeline_layout_finish(device, pipeline_layout);
   return result;
}

static VkResult
radv_graphics_lib_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                                  const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_graphics_lib_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_init(device, &pipeline->base.base, RADV_PIPELINE_GRAPHICS_LIB);

   result = radv_graphics_lib_pipeline_init(pipeline, device, cache, pCreateInfo);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base.base, pAllocator);
      return result;
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base.base);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateGraphicsPipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                             const VkGraphicsPipelineCreateInfo *pCreateInfos,
                             const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r;
      if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
         r = radv_graphics_lib_pipeline_create(_device, pipelineCache, &pCreateInfos[i],
                                               pAllocator, &pPipelines[i]);
      } else {
         r = radv_graphics_pipeline_create(_device, pipelineCache, &pCreateInfos[i], NULL,
                                           pAllocator, &pPipelines[i]);
      }
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

void
radv_pipeline_emit_hw_cs(const struct radv_physical_device *pdevice, struct radeon_cmdbuf *cs,
                         const struct radv_shader *shader)
{
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg(cs, R_00B830_COMPUTE_PGM_LO, va >> 8);

   radeon_set_sh_reg_seq(cs, R_00B848_COMPUTE_PGM_RSRC1, 2);
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);
   if (pdevice->rad_info.gfx_level >= GFX10) {
      radeon_set_sh_reg(cs, R_00B8A0_COMPUTE_PGM_RSRC3, shader->config.rsrc3);
   }
}

void
radv_pipeline_emit_compute_state(const struct radv_physical_device *pdevice,
                                 struct radeon_cmdbuf *cs, const struct radv_shader *shader)
{
   unsigned threads_per_threadgroup;
   unsigned threadgroups_per_cu = 1;
   unsigned waves_per_threadgroup;
   unsigned max_waves_per_sh = 0;

   /* Calculate best compute resource limits. */
   threads_per_threadgroup =
      shader->info.cs.block_size[0] * shader->info.cs.block_size[1] * shader->info.cs.block_size[2];
   waves_per_threadgroup = DIV_ROUND_UP(threads_per_threadgroup, shader->info.wave_size);

   if (pdevice->rad_info.gfx_level >= GFX10 && waves_per_threadgroup == 1)
      threadgroups_per_cu = 2;

   radeon_set_sh_reg(
      cs, R_00B854_COMPUTE_RESOURCE_LIMITS,
      ac_get_compute_resource_limits(&pdevice->rad_info, waves_per_threadgroup,
                                     max_waves_per_sh, threadgroups_per_cu));

   radeon_set_sh_reg_seq(cs, R_00B81C_COMPUTE_NUM_THREAD_X, 3);
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[0]));
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[1]));
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[2]));
}

static void
radv_compute_generate_pm4(struct radv_compute_pipeline *pipeline)
{
   struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *shader = pipeline->base.shaders[MESA_SHADER_COMPUTE];
   struct radeon_cmdbuf *cs = &pipeline->base.cs;

   cs->max_dw = pdevice->rad_info.gfx_level >= GFX10 ? 19 : 16;
   cs->buf = malloc(cs->max_dw * 4);

   radv_pipeline_emit_hw_cs(pdevice, cs, shader);
   radv_pipeline_emit_compute_state(pdevice, cs, shader);

   assert(pipeline->base.cs.cdw <= pipeline->base.cs.max_dw);
}

static struct radv_pipeline_key
radv_generate_compute_pipeline_key(struct radv_compute_pipeline *pipeline,
                                   const VkComputePipelineCreateInfo *pCreateInfo)
{
   const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->stage;
   struct radv_pipeline_key key = radv_generate_pipeline_key(&pipeline->base, pCreateInfo->flags);

   const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *subgroup_size =
      vk_find_struct_const(stage->pNext,
                           PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);

   if (subgroup_size) {
      assert(subgroup_size->requiredSubgroupSize == 32 ||
             subgroup_size->requiredSubgroupSize == 64);
      key.cs.compute_subgroup_size = subgroup_size->requiredSubgroupSize;
   } else if (stage->flags & VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT) {
      key.cs.require_full_subgroups = true;
   }

   return key;
}

VkResult
radv_compute_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                             const VkComputePipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator, const uint8_t *custom_hash,
                             struct radv_pipeline_shader_stack_size *rt_stack_sizes,
                             uint32_t rt_group_count, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   struct radv_compute_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL) {
      free(rt_stack_sizes);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_COMPUTE);

   pipeline->rt_stack_sizes = rt_stack_sizes;
   pipeline->group_count = rt_group_count;

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   struct radv_pipeline_key key = radv_generate_compute_pipeline_key(pipeline, pCreateInfo);

   UNUSED gl_shader_stage last_vgt_api_stage = MESA_SHADER_NONE;
   result = radv_create_shaders(&pipeline->base, pipeline_layout, device, cache, &key, &pCreateInfo->stage,
                                1, pCreateInfo->flags, custom_hash, creation_feedback,
                                &pipeline->rt_stack_sizes, &pipeline->group_count,
                                &last_vgt_api_stage);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base, pAllocator);
      return result;
   }

   pipeline->base.user_data_0[MESA_SHADER_COMPUTE] = R_00B900_COMPUTE_USER_DATA_0;
   pipeline->base.need_indirect_descriptor_sets |=
      radv_shader_need_indirect_descriptor_sets(&pipeline->base, MESA_SHADER_COMPUTE);
   radv_pipeline_init_scratch(device, &pipeline->base);

   pipeline->base.push_constant_size = pipeline_layout->push_constant_size;
   pipeline->base.dynamic_offset_count = pipeline_layout->dynamic_offset_count;

   if (device->physical_device->rad_info.has_cs_regalloc_hang_bug) {
      struct radv_shader *compute_shader = pipeline->base.shaders[MESA_SHADER_COMPUTE];
      unsigned *cs_block_size = compute_shader->info.cs.block_size;

      pipeline->cs_regalloc_hang_bug = cs_block_size[0] * cs_block_size[1] * cs_block_size[2] > 256;
   }

   radv_compute_generate_pm4(pipeline);

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateComputePipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                            const VkComputePipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = radv_compute_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator, NULL,
                                       NULL, 0, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

static uint32_t
radv_get_executable_count(struct radv_pipeline *pipeline)
{
   uint32_t ret = 0;
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (!pipeline->shaders[i])
         continue;

      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         ret += 2u;
      } else {
         ret += 1u;
      }
   }
   return ret;
}

static struct radv_shader *
radv_get_shader_from_executable_index(struct radv_pipeline *pipeline, int index,
                                      gl_shader_stage *stage)
{
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (!pipeline->shaders[i])
         continue;
      if (!index) {
         *stage = i;
         return pipeline->shaders[i];
      }

      --index;

      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         if (!index) {
            *stage = i;
            return pipeline->gs_copy_shader;
         }
         --index;
      }
   }

   *stage = -1;
   return NULL;
}

/* Basically strlcpy (which does not exist on linux) specialized for
 * descriptions. */
static void
desc_copy(char *desc, const char *src)
{
   int len = strlen(src);
   assert(len < VK_MAX_DESCRIPTION_SIZE);
   memcpy(desc, src, len);
   memset(desc + len, 0, VK_MAX_DESCRIPTION_SIZE - len);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutablePropertiesKHR(VkDevice _device, const VkPipelineInfoKHR *pPipelineInfo,
                                        uint32_t *pExecutableCount,
                                        VkPipelineExecutablePropertiesKHR *pProperties)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pPipelineInfo->pipeline);
   const uint32_t total_count = radv_get_executable_count(pipeline);

   if (!pProperties) {
      *pExecutableCount = total_count;
      return VK_SUCCESS;
   }

   const uint32_t count = MIN2(total_count, *pExecutableCount);
   for (unsigned i = 0, executable_idx = 0; i < MESA_VULKAN_SHADER_STAGES && executable_idx < count; ++i) {
      if (!pipeline->shaders[i])
         continue;
      pProperties[executable_idx].stages = mesa_to_vk_shader_stage(i);
      const char *name = NULL;
      const char *description = NULL;
      switch (i) {
      case MESA_SHADER_VERTEX:
         name = "Vertex Shader";
         description = "Vulkan Vertex Shader";
         break;
      case MESA_SHADER_TESS_CTRL:
         if (!pipeline->shaders[MESA_SHADER_VERTEX]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_VERTEX_BIT;
            name = "Vertex + Tessellation Control Shaders";
            description = "Combined Vulkan Vertex and Tessellation Control Shaders";
         } else {
            name = "Tessellation Control Shader";
            description = "Vulkan Tessellation Control Shader";
         }
         break;
      case MESA_SHADER_TESS_EVAL:
         name = "Tessellation Evaluation Shader";
         description = "Vulkan Tessellation Evaluation Shader";
         break;
      case MESA_SHADER_GEOMETRY:
         if (pipeline->shaders[MESA_SHADER_TESS_CTRL] && !pipeline->shaders[MESA_SHADER_TESS_EVAL]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            name = "Tessellation Evaluation + Geometry Shaders";
            description = "Combined Vulkan Tessellation Evaluation and Geometry Shaders";
         } else if (!pipeline->shaders[MESA_SHADER_TESS_CTRL] && !pipeline->shaders[MESA_SHADER_VERTEX]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_VERTEX_BIT;
            name = "Vertex + Geometry Shader";
            description = "Combined Vulkan Vertex and Geometry Shaders";
         } else {
            name = "Geometry Shader";
            description = "Vulkan Geometry Shader";
         }
         break;
      case MESA_SHADER_FRAGMENT:
         name = "Fragment Shader";
         description = "Vulkan Fragment Shader";
         break;
      case MESA_SHADER_COMPUTE:
         name = "Compute Shader";
         description = "Vulkan Compute Shader";
         break;
      case MESA_SHADER_MESH:
         name = "Mesh Shader";
         description = "Vulkan Mesh Shader";
         break;
      case MESA_SHADER_TASK:
         name = "Task Shader";
         description = "Vulkan Task Shader";
         break;
      }

      pProperties[executable_idx].subgroupSize = pipeline->shaders[i]->info.wave_size;
      desc_copy(pProperties[executable_idx].name, name);
      desc_copy(pProperties[executable_idx].description, description);

      ++executable_idx;
      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         assert(pipeline->gs_copy_shader);
         if (executable_idx >= count)
            break;

         pProperties[executable_idx].stages = VK_SHADER_STAGE_GEOMETRY_BIT;
         pProperties[executable_idx].subgroupSize = 64;
         desc_copy(pProperties[executable_idx].name, "GS Copy Shader");
         desc_copy(pProperties[executable_idx].description,
                   "Extra shader stage that loads the GS output ringbuffer into the rasterizer");

         ++executable_idx;
      }
   }

   VkResult result = *pExecutableCount < total_count ? VK_INCOMPLETE : VK_SUCCESS;
   *pExecutableCount = count;
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutableStatisticsKHR(VkDevice _device,
                                        const VkPipelineExecutableInfoKHR *pExecutableInfo,
                                        uint32_t *pStatisticCount,
                                        VkPipelineExecutableStatisticKHR *pStatistics)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pExecutableInfo->pipeline);
   gl_shader_stage stage;
   struct radv_shader *shader =
      radv_get_shader_from_executable_index(pipeline, pExecutableInfo->executableIndex, &stage);

   const struct radv_physical_device *pdevice = device->physical_device;

   unsigned lds_increment = pdevice->rad_info.gfx_level >= GFX11 && stage == MESA_SHADER_FRAGMENT
      ? 1024 : pdevice->rad_info.lds_encode_granularity;
   unsigned max_waves = radv_get_max_waves(device, shader, stage);

   VkPipelineExecutableStatisticKHR *s = pStatistics;
   VkPipelineExecutableStatisticKHR *end = s + (pStatistics ? *pStatisticCount : 0);
   VkResult result = VK_SUCCESS;

   if (s < end) {
      desc_copy(s->name, "Driver pipeline hash");
      desc_copy(s->description, "Driver pipeline hash used by RGP");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = pipeline->pipeline_hash;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "SGPRs");
      desc_copy(s->description, "Number of SGPR registers allocated per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.num_sgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "VGPRs");
      desc_copy(s->description, "Number of VGPR registers allocated per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.num_vgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Spilled SGPRs");
      desc_copy(s->description, "Number of SGPR registers spilled per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.spilled_sgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Spilled VGPRs");
      desc_copy(s->description, "Number of VGPR registers spilled per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.spilled_vgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Code size");
      desc_copy(s->description, "Code size in bytes");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->exec_size;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "LDS size");
      desc_copy(s->description, "LDS size in bytes per workgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.lds_size * lds_increment;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Scratch size");
      desc_copy(s->description, "Private memory in bytes per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.scratch_bytes_per_wave;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Subgroups per SIMD");
      desc_copy(s->description, "The maximum number of subgroups in flight on a SIMD unit");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = max_waves;
   }
   ++s;

   if (shader->statistics) {
      for (unsigned i = 0; i < aco_num_statistics; i++) {
         const struct aco_compiler_statistic_info *info = &aco_statistic_infos[i];
         if (s < end) {
            desc_copy(s->name, info->name);
            desc_copy(s->description, info->desc);
            s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
            s->value.u64 = shader->statistics[i];
         }
         ++s;
      }
   }

   if (!pStatistics)
      *pStatisticCount = s - pStatistics;
   else if (s > end) {
      *pStatisticCount = end - pStatistics;
      result = VK_INCOMPLETE;
   } else {
      *pStatisticCount = s - pStatistics;
   }

   return result;
}

static VkResult
radv_copy_representation(void *data, size_t *data_size, const char *src)
{
   size_t total_size = strlen(src) + 1;

   if (!data) {
      *data_size = total_size;
      return VK_SUCCESS;
   }

   size_t size = MIN2(total_size, *data_size);

   memcpy(data, src, size);
   if (size)
      *((char *)data + size - 1) = 0;
   return size < total_size ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutableInternalRepresentationsKHR(
   VkDevice _device, const VkPipelineExecutableInfoKHR *pExecutableInfo,
   uint32_t *pInternalRepresentationCount,
   VkPipelineExecutableInternalRepresentationKHR *pInternalRepresentations)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pExecutableInfo->pipeline);
   gl_shader_stage stage;
   struct radv_shader *shader =
      radv_get_shader_from_executable_index(pipeline, pExecutableInfo->executableIndex, &stage);

   VkPipelineExecutableInternalRepresentationKHR *p = pInternalRepresentations;
   VkPipelineExecutableInternalRepresentationKHR *end =
      p + (pInternalRepresentations ? *pInternalRepresentationCount : 0);
   VkResult result = VK_SUCCESS;
   /* optimized NIR */
   if (p < end) {
      p->isText = true;
      desc_copy(p->name, "NIR Shader(s)");
      desc_copy(p->description, "The optimized NIR shader(s)");
      if (radv_copy_representation(p->pData, &p->dataSize, shader->nir_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   /* backend IR */
   if (p < end) {
      p->isText = true;
      if (radv_use_llvm_for_stage(device, stage)) {
         desc_copy(p->name, "LLVM IR");
         desc_copy(p->description, "The LLVM IR after some optimizations");
      } else {
         desc_copy(p->name, "ACO IR");
         desc_copy(p->description, "The ACO IR after some optimizations");
      }
      if (radv_copy_representation(p->pData, &p->dataSize, shader->ir_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   /* Disassembler */
   if (p < end && shader->disasm_string) {
      p->isText = true;
      desc_copy(p->name, "Assembly");
      desc_copy(p->description, "Final Assembly");
      if (radv_copy_representation(p->pData, &p->dataSize, shader->disasm_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   if (!pInternalRepresentations)
      *pInternalRepresentationCount = p - pInternalRepresentations;
   else if (p > end) {
      result = VK_INCOMPLETE;
      *pInternalRepresentationCount = end - pInternalRepresentations;
   } else {
      *pInternalRepresentationCount = p - pInternalRepresentations;
   }

   return result;
}
