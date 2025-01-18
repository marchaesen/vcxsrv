/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_alloc.h"
#include "panvk_cmd_fb_preload.h"
#include "panvk_image_view.h"
#include "panvk_meta.h"
#include "panvk_shader.h"

#include "nir_builder.h"

#include "pan_shader.h"

struct panvk_fb_preload_shader_key {
   enum panvk_meta_object_key_type type;
   VkImageViewType view_type;
   VkSampleCountFlagBits samples;
   VkImageAspectFlags aspects;
   bool needs_layer_id;
   struct {
      nir_alu_type type;
   } color[8];
};

static nir_def *
texel_fetch(nir_builder *b, VkImageViewType view_type,
            nir_alu_type reg_type, unsigned tex_idx,
            nir_def *sample_id, nir_def *coords)
{
   nir_tex_instr *tex = nir_tex_instr_create(b->shader, sample_id ? 3 : 2);

   tex->op = sample_id ? nir_texop_txf_ms : nir_texop_txf;
   tex->dest_type = reg_type;
   tex->is_array = vk_image_view_type_is_array(view_type);
   tex->sampler_dim = sample_id ? GLSL_SAMPLER_DIM_MS
                                : vk_image_view_type_to_sampler_dim(view_type);
   tex->coord_components = coords->num_components;
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, coords);
   tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_int(b, 0));

   if (sample_id)
      tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_ms_index, sample_id);

#if PAN_ARCH <= 7
   tex->sampler_index = 0;
   tex->texture_index = tex_idx;
#else
   tex->sampler_index = pan_res_handle(0, 0);
   tex->texture_index = pan_res_handle(0, tex_idx + 1);
#endif

   nir_def_init(&tex->instr, &tex->def, 4, 32);
   nir_builder_instr_insert(b, &tex->instr);

   return &tex->def;
}

static nir_variable *
color_output_var(nir_builder *b, VkImageViewType view_type,
                 VkImageAspectFlags aspect, VkSampleCountFlagBits samples,
                 nir_alu_type fmt_type, unsigned rt)
{
   enum glsl_base_type base_type =
      nir_get_glsl_base_type_for_nir_type(fmt_type);
   const struct glsl_type *var_type = glsl_vector_type(base_type, 4);
   static const char *var_names[] = {
      "gl_FragData[0]", "gl_FragData[1]", "gl_FragData[2]", "gl_FragData[3]",
      "gl_FragData[4]", "gl_FragData[5]", "gl_FragData[6]", "gl_FragData[7]",
   };

   assert(rt < ARRAY_SIZE(var_names));

   nir_variable *var = nir_variable_create(b->shader, nir_var_shader_out,
                                           var_type, var_names[rt]);
   var->data.location = FRAG_RESULT_DATA0 + rt;

   return var;
}

static nir_def *
get_layer_id(nir_builder *b)
{
#if PAN_ARCH <= 7
   return nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0), .base = 0,
                                 .range = 4);
#else
   return nir_load_layer_id(b);
#endif
}

static nir_shader *
get_preload_nir_shader(const struct panvk_fb_preload_shader_key *key)
{
   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, GENX(pan_shader_get_compiler_options)(),
      "panvk-meta-preload");
   nir_builder *b = &builder;
   nir_def *sample_id =
      key->samples != VK_SAMPLE_COUNT_1_BIT ? nir_load_sample_id(b) : NULL;
   nir_def *coords = nir_u2u32(b, nir_load_pixel_coord(b));

   if (key->view_type == VK_IMAGE_VIEW_TYPE_2D_ARRAY ||
       key->view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ||
       key->view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
       key->view_type == VK_IMAGE_VIEW_TYPE_3D) {
      coords =
         nir_vec3(b, nir_channel(b, coords, 0), nir_channel(b, coords, 1),
                  key->needs_layer_id ? get_layer_id(b) : nir_imm_int(b, 0));
   }

   if (key->aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
      for (uint32_t i = 0; i < ARRAY_SIZE(key->color); i++) {
         if (key->color[i].type == nir_type_invalid)
            continue;

         nir_def *texel = texel_fetch(b, key->view_type, key->color[i].type, i,
                                      sample_id, coords);

         nir_store_output(
            b, texel, nir_imm_int(b, 0), .base = i,
            .src_type = key->color[i].type,
            .io_semantics.location = FRAG_RESULT_DATA0 + i,
            .io_semantics.num_slots = 1,
            .write_mask = nir_component_mask(texel->num_components));
      }
   }

   if (key->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      nir_def *texel = texel_fetch(b, key->view_type, nir_type_float32, 0,
                                   sample_id, coords);

      nir_store_output(b, nir_channel(b, texel, 0), nir_imm_int(b, 0),
                       .base = 0, .src_type = nir_type_float32,
                       .io_semantics.location = FRAG_RESULT_DEPTH,
                       .io_semantics.num_slots = 1,
                       .write_mask = nir_component_mask(1));
   }

   if (key->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      nir_def *texel = texel_fetch(
         b, key->view_type, nir_type_uint32,
         key->aspects & VK_IMAGE_ASPECT_DEPTH_BIT ? 1 : 0, sample_id, coords);

      nir_store_output(b, nir_channel(b, texel, 0), nir_imm_int(b, 0),
                       .base = 0, .src_type = nir_type_uint32,
                       .io_semantics.location = FRAG_RESULT_STENCIL,
                       .io_semantics.num_slots = 1,
                       .write_mask = nir_component_mask(1));
   }

   return b->shader;
}

static VkResult
get_preload_shader(struct panvk_device *dev,
                   const struct panvk_fb_preload_shader_key *key,
                   struct panvk_internal_shader **shader_out)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_internal_shader *shader;
   VkShaderEXT shader_handle = (VkShaderEXT)vk_meta_lookup_object(
      &dev->meta, VK_OBJECT_TYPE_SHADER_EXT, key, sizeof(*key));
   if (shader_handle != VK_NULL_HANDLE)
      goto out;

   nir_shader *nir = get_preload_nir_shader(key);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   struct panfrost_compile_inputs inputs = {
      .gpu_id = phys_dev->kmod.props.gpu_prod_id,
      .no_ubo_to_push = true,
      .is_blit = true,
   };

   pan_shader_preprocess(nir, inputs.gpu_id);

   VkResult result = panvk_per_arch(create_internal_shader)(
      dev, nir, &inputs, &shader);
   if (result != VK_SUCCESS)
      return result;

#if PAN_ARCH >= 9
   shader->spd = panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
   if (!panvk_priv_mem_host_addr(shader->spd)) {
      vk_shader_destroy(&dev->vk, &shader->vk, NULL);
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   pan_pack(panvk_priv_mem_host_addr(shader->spd), SHADER_PROGRAM, cfg) {
      cfg.stage = MALI_SHADER_STAGE_FRAGMENT;
      cfg.fragment_coverage_bitmask_type = MALI_COVERAGE_BITMASK_TYPE_GL;
      cfg.register_allocation = MALI_SHADER_REGISTER_ALLOCATION_32_PER_THREAD;
      cfg.binary = panvk_priv_mem_dev_addr(shader->code_mem);
      cfg.preload.r48_r63 = shader->info.preload >> 48;
   }
#endif

   shader_handle = (VkShaderEXT)vk_meta_cache_object(
      &dev->vk, &dev->meta, key, sizeof(*key), VK_OBJECT_TYPE_SHADER_EXT,
      (uint64_t)panvk_internal_shader_to_handle(shader));

out:
   shader = panvk_internal_shader_from_handle(shader_handle);
   *shader_out = shader;
   return VK_SUCCESS;
}

static VkResult
alloc_pre_post_dcds(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   if (fbinfo->bifrost.pre_post.dcds.gpu)
      return VK_SUCCESS;

   uint32_t dcd_count =
      3 * (PAN_ARCH <= 7 ? cmdbuf->state.gfx.render.layer_count : 1);

   fbinfo->bifrost.pre_post.dcds = panvk_cmd_alloc_desc_array(cmdbuf, dcd_count, DRAW);
   if (!fbinfo->bifrost.pre_post.dcds.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   return VK_SUCCESS;
}

static enum mali_register_file_format
get_reg_fmt(nir_alu_type type)
{
   switch (type) {
   case nir_type_float32:
      return MALI_REGISTER_FILE_FORMAT_F32;
   case nir_type_uint32:
      return MALI_REGISTER_FILE_FORMAT_U32;
   case nir_type_int32:
      return MALI_REGISTER_FILE_FORMAT_I32;
   default:
      assert(!"Invalid reg type");
      return MALI_REGISTER_FILE_FORMAT_F32;
   }
}

static void
fill_textures(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_fb_preload_shader_key *key,
              struct mali_texture_packed *textures)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   if (key->aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
      for (unsigned i = 0; i < fbinfo->rt_count; i++) {
         struct panvk_image_view *iview =
            cmdbuf->state.gfx.render.color_attachments.iviews[i];

         if (iview)
            textures[i] = iview->descs.tex;
         else
            textures[i] = (struct mali_texture_packed){0};
      }
      return;
   }

   uint32_t idx = 0;
   if (key->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      struct panvk_image_view *iview =
         cmdbuf->state.gfx.render.z_attachment.iview
            ?: cmdbuf->state.gfx.render.s_attachment.iview;

      textures[idx++] = vk_format_has_depth(iview->vk.view_format)
                           ? iview->descs.tex
                           : iview->descs.other_aspect_tex;
   }

   if (key->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      struct panvk_image_view *iview =
         cmdbuf->state.gfx.render.s_attachment.iview
            ?: cmdbuf->state.gfx.render.z_attachment.iview;

      textures[idx++] = vk_format_has_depth(iview->vk.view_format)
                           ? iview->descs.other_aspect_tex
                           : iview->descs.tex;
   }
}

static void
fill_bds(struct panvk_cmd_buffer *cmdbuf,
         const struct panvk_fb_preload_shader_key *key,
         struct mali_blend_packed *bds)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   uint32_t bd_count = MAX2(fbinfo->rt_count, 1);

   for (unsigned i = 0; i < bd_count; i++) {
      const struct pan_image_view *pview =
         fbinfo->rts[i].preload ? fbinfo->rts[i].view : NULL;

      pan_pack(&bds[i], BLEND, cfg) {
         if (key->aspects != VK_IMAGE_ASPECT_COLOR_BIT || !pview) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
            continue;
         }

         cfg.round_to_fb_precision = true;
         cfg.srgb = util_format_is_srgb(pview->format);
         cfg.internal.mode = MALI_BLEND_MODE_OPAQUE;
         cfg.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
         cfg.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
         cfg.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
         cfg.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
         cfg.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
         cfg.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
         cfg.equation.color_mask = 0xf;

         cfg.internal.fixed_function.num_comps = 4;
         cfg.internal.fixed_function.conversion.memory_format = GENX(
            panfrost_dithered_format_from_pipe_format)(pview->format, false);
         cfg.internal.fixed_function.rt = i;
#if PAN_ARCH <= 7
         cfg.internal.fixed_function.conversion.register_format =
            get_reg_fmt(key->color[i].type);
#endif
      }
   }
}

#if PAN_ARCH <= 7
static VkResult
cmd_emit_dcd(struct panvk_cmd_buffer *cmdbuf,
             const struct panvk_fb_preload_shader_key *key)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   struct panvk_internal_shader *shader = NULL;

   VkResult result = get_preload_shader(dev, key, &shader);
   if (result != VK_SUCCESS)
      return result;

   uint32_t tex_count = key->aspects == VK_IMAGE_ASPECT_COLOR_BIT
                           ? fbinfo->rt_count
                           : util_bitcount(key->aspects);
   uint32_t bd_count = MAX2(fbinfo->rt_count, 1);

   struct panfrost_ptr rsd = panvk_cmd_alloc_desc_aggregate(
      cmdbuf, PAN_DESC(RENDERER_STATE),
      PAN_DESC_ARRAY(bd_count, BLEND));
   if (!rsd.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_pack(rsd.cpu, RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(&shader->info,
                             panvk_priv_mem_dev_addr(shader->code_mem), &cfg);

      cfg.shader.texture_count = tex_count;
      cfg.shader.sampler_count = 1;

      cfg.multisample_misc.sample_mask = 0xFFFF;
      cfg.multisample_misc.multisample_enable = key->samples > 1;
      cfg.multisample_misc.evaluate_per_sample = key->samples > 1;

      cfg.multisample_misc.depth_function = MALI_FUNC_ALWAYS;
      cfg.multisample_misc.depth_write_mask =
         (key->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;

      cfg.stencil_mask_misc.stencil_enable =
         (key->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
      cfg.stencil_mask_misc.stencil_mask_front = 0xFF;
      cfg.stencil_mask_misc.stencil_mask_back = 0xFF;
      cfg.stencil_front.compare_function = MALI_FUNC_ALWAYS;
      cfg.stencil_front.stencil_fail = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.depth_fail = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.depth_pass = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.mask = 0xFF;

      cfg.stencil_back = cfg.stencil_front;

      if (key->aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
         /* Skipping ATEST requires forcing Z/S */
         cfg.properties.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
         cfg.properties.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
      } else {
         /* Writing Z/S requires late updates */
         cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
         cfg.properties.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_LATE;
      }

      /* However, while shaders writing Z/S can normally be killed, on v6
       * for frame shaders it can cause GPU timeouts, so only allow colour
       * blit shaders to be killed. */
      cfg.properties.allow_forward_pixel_to_kill =
         key->aspects == VK_IMAGE_ASPECT_COLOR_BIT;

      if (PAN_ARCH == 6)
         cfg.properties.allow_forward_pixel_to_be_killed =
            key->aspects == VK_IMAGE_ASPECT_COLOR_BIT;
   }

   fill_bds(cmdbuf, key, rsd.cpu + pan_size(RENDERER_STATE));

   struct panvk_batch *batch = cmdbuf->cur_batch;
   uint16_t minx = 0, miny = 0, maxx, maxy;
 
   /* Align on 32x32 tiles */
   minx = fbinfo->extent.minx & ~31;
   miny = fbinfo->extent.miny & ~31;
   maxx = MIN2(ALIGN_POT(fbinfo->extent.maxx + 1, 32), fbinfo->width) - 1;
   maxy = MIN2(ALIGN_POT(fbinfo->extent.maxy + 1, 32), fbinfo->height) - 1;

   struct panfrost_ptr vpd = panvk_cmd_alloc_desc(cmdbuf, VIEWPORT);
   if (!vpd.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_pack(vpd.cpu, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
   }

   struct panfrost_ptr sampler = panvk_cmd_alloc_desc(cmdbuf, SAMPLER);
   if (!sampler.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_pack(sampler.cpu, SAMPLER, cfg) {
      cfg.seamless_cube_map = false;
      cfg.normalized_coordinates = false;
      cfg.minify_nearest = true;
      cfg.magnify_nearest = true;
   }

   struct panfrost_ptr textures =
      panvk_cmd_alloc_desc_array(cmdbuf, tex_count, TEXTURE);
   if (!textures.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   fill_textures(cmdbuf, key, textures.cpu);

   result = alloc_pre_post_dcds(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   struct mali_draw_packed dcd_base;

   pan_pack(&dcd_base, DRAW, cfg) {
      cfg.thread_storage = batch->tls.gpu;
      cfg.state = rsd.gpu;

      cfg.viewport = vpd.gpu;

      cfg.textures = textures.gpu;
      cfg.samplers = sampler.gpu;

#if PAN_ARCH >= 6
      /* Until we decide to support FB CRC, we can consider that untouched tiles
       * should never be written back. */
      cfg.clean_fragment_write = true;
#endif
   }

   struct mali_draw_packed *dcds = fbinfo->bifrost.pre_post.dcds.cpu;
   uint32_t dcd_idx = key->aspects == VK_IMAGE_ASPECT_COLOR_BIT ? 0 : 1;

   if (key->needs_layer_id) {
      struct panfrost_ptr layer_ids = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc,
         cmdbuf->state.gfx.render.layer_count * sizeof(uint64_t),
         sizeof(uint64_t));
      uint32_t *layer_id = layer_ids.cpu;

      for (uint32_t l = 0; l < cmdbuf->state.gfx.render.layer_count; l++) {
         struct mali_draw_packed dcd_layer;

         /* Push uniform pointer has to be 8-byte aligned, so we have to skip
          * odd layer_id entries. */
         layer_id[2 * l] = l;
         pan_pack(&dcd_layer, DRAW, cfg) {
            cfg.push_uniforms = layer_ids.gpu + (sizeof(uint64_t) * l);
         };

         pan_merge(dcd_layer, dcd_base, DRAW);
	 dcds[(l * 3) + dcd_idx] = dcd_layer;
      }
   } else {
      dcds[dcd_idx] = dcd_base;
   }

   if (key->aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
      fbinfo->bifrost.pre_post.modes[dcd_idx] =
         MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT;
   } else {
      enum pipe_format fmt = fbinfo->zs.view.zs
                                ? fbinfo->zs.view.zs->planes[0]->layout.format
                                : fbinfo->zs.view.s->planes[0]->layout.format;
      bool always = false;

      /* If we're dealing with a combined ZS resource and only one
       * component is cleared, we need to reload the whole surface
       * because the zs_clean_pixel_write_enable flag is set in that
       * case.
       */
      if (util_format_is_depth_and_stencil(fmt) &&
          fbinfo->zs.clear.z != fbinfo->zs.clear.s)
         always = true;

      /* We could use INTERSECT on Bifrost v7 too, but
       * EARLY_ZS_ALWAYS has the advantage of reloading the ZS tile
       * buffer one or more tiles ahead, making ZS data immediately
       * available for any ZS tests taking place in other shaders.
       * Thing's haven't been benchmarked to determine what's
       * preferable (saving bandwidth vs having ZS preloaded
       * earlier), so let's leave it like that for now.
       */
      fbinfo->bifrost.pre_post.modes[dcd_idx] =
         PAN_ARCH > 6
            ? MALI_PRE_POST_FRAME_SHADER_MODE_EARLY_ZS_ALWAYS
         : always ? MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS
                  : MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT;
   }

   return VK_SUCCESS;
}
#else
static VkResult
cmd_emit_dcd(struct panvk_cmd_buffer *cmdbuf,
             struct panvk_fb_preload_shader_key *key)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   struct panvk_internal_shader *shader = NULL;

   VkResult result = get_preload_shader(dev, key, &shader);
   if (result != VK_SUCCESS)
      return result;

   uint32_t bd_count =
      key->aspects == VK_IMAGE_ASPECT_COLOR_BIT ? fbinfo->rt_count : 0;
   struct panfrost_ptr bds =
      panvk_cmd_alloc_desc_array(cmdbuf, bd_count, BLEND);
   if (bd_count > 0 && !bds.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   uint32_t tex_count = key->aspects == VK_IMAGE_ASPECT_COLOR_BIT
                           ? fbinfo->rt_count
                           : util_bitcount(key->aspects);
   uint32_t desc_count = tex_count + 1;

   struct panfrost_ptr descs = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   if (!descs.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_sampler_packed *sampler = descs.cpu;

   pan_pack(sampler, SAMPLER, cfg) {
      cfg.seamless_cube_map = false;
      cfg.normalized_coordinates = false;
      cfg.minify_nearest = true;
      cfg.magnify_nearest = true;
   }

   fill_textures(cmdbuf, key, descs.cpu + PANVK_DESCRIPTOR_SIZE);

   if (key->aspects == VK_IMAGE_ASPECT_COLOR_BIT)
      fill_bds(cmdbuf, key, bds.cpu);

   struct panfrost_ptr res_table = panvk_cmd_alloc_desc(cmdbuf, RESOURCE);
   if (!res_table.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_pack(res_table.cpu, RESOURCE, cfg) {
      cfg.address = descs.gpu;
      cfg.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   }

   struct panfrost_ptr zsd = panvk_cmd_alloc_desc(cmdbuf, DEPTH_STENCIL);
   if (!zsd.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   bool preload_z =
      key->aspects != VK_IMAGE_ASPECT_COLOR_BIT && fbinfo->zs.preload.z;
   bool preload_s =
      key->aspects != VK_IMAGE_ASPECT_COLOR_BIT && fbinfo->zs.preload.s;

   pan_pack(zsd.cpu, DEPTH_STENCIL, cfg) {
      cfg.depth_function = MALI_FUNC_ALWAYS;
      cfg.depth_write_enable = preload_z;

      if (preload_z)
         cfg.depth_source = MALI_DEPTH_SOURCE_SHADER;

      cfg.stencil_test_enable = preload_s;
      cfg.stencil_from_shader = preload_s;

      cfg.front_compare_function = MALI_FUNC_ALWAYS;
      cfg.front_stencil_fail = MALI_STENCIL_OP_REPLACE;
      cfg.front_depth_fail = MALI_STENCIL_OP_REPLACE;
      cfg.front_depth_pass = MALI_STENCIL_OP_REPLACE;
      cfg.front_write_mask = 0xFF;
      cfg.front_value_mask = 0xFF;

      cfg.back_compare_function = MALI_FUNC_ALWAYS;
      cfg.back_stencil_fail = MALI_STENCIL_OP_REPLACE;
      cfg.back_depth_fail = MALI_STENCIL_OP_REPLACE;
      cfg.back_depth_pass = MALI_STENCIL_OP_REPLACE;
      cfg.back_write_mask = 0xFF;
      cfg.back_value_mask = 0xFF;

      cfg.depth_cull_enable = false;
   }

   result = alloc_pre_post_dcds(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   struct mali_draw_packed *dcds = fbinfo->bifrost.pre_post.dcds.cpu;
   uint32_t dcd_idx = key->aspects == VK_IMAGE_ASPECT_COLOR_BIT ? 0 : 1;

   pan_pack(&dcds[dcd_idx], DRAW, cfg) {
      if (key->aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
         /* Skipping ATEST requires forcing Z/S */
         cfg.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
         cfg.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;

         cfg.blend = bds.gpu;
         cfg.blend_count = bd_count;
         cfg.render_target_mask = cmdbuf->state.gfx.render.bound_attachments &
                                  MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS;
      } else {
         /* ZS_EMIT requires late update/kill */
         cfg.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
         cfg.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_LATE;
         cfg.blend_count = 0;
      }

      cfg.allow_forward_pixel_to_kill =
         key->aspects == VK_IMAGE_ASPECT_COLOR_BIT;
      cfg.allow_forward_pixel_to_be_killed = true;
      cfg.depth_stencil = zsd.gpu;
      cfg.sample_mask = 0xFFFF;
      cfg.multisample_enable = key->samples > 1;
      cfg.evaluate_per_sample = key->samples > 1;
      cfg.maximum_z = 1.0;
      cfg.clean_fragment_write = true;
      cfg.shader.resources = res_table.gpu | 1;
      cfg.shader.shader = panvk_priv_mem_dev_addr(shader->spd);
      cfg.shader.thread_storage = cmdbuf->state.gfx.tsd;
   }

   if (key->aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
      fbinfo->bifrost.pre_post.modes[dcd_idx] =
         MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT;
   } else {
      /* We could use INTERSECT on Valhall too, but
       * EARLY_ZS_ALWAYS has the advantage of reloading the ZS tile
       * buffer one or more tiles ahead, making ZS data immediately
       * available for any ZS tests taking place in other shaders.
       * Thing's haven't been benchmarked to determine what's
       * preferable (saving bandwidth vs having ZS preloaded
       * earlier), so let's leave it like that for now.
       */
      fbinfo->bifrost.pre_post.modes[dcd_idx] =
         MALI_PRE_POST_FRAME_SHADER_MODE_EARLY_ZS_ALWAYS;
   }

   return VK_SUCCESS;
}
#endif

static VkResult
cmd_preload_zs_attachments(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   if (!fbinfo->zs.preload.s && !fbinfo->zs.preload.z)
      return VK_SUCCESS;

   struct panvk_fb_preload_shader_key key = {
      .type = PANVK_META_OBJECT_KEY_FB_PRELOAD_SHADER,
      .samples = fbinfo->nr_samples,
      .needs_layer_id = cmdbuf->state.gfx.render.layer_count > 1,
   };

   if (fbinfo->zs.preload.z) {
      key.aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
      key.view_type =
         cmdbuf->state.gfx.render.z_attachment.iview
            ? cmdbuf->state.gfx.render.z_attachment.iview->vk.view_type
            : cmdbuf->state.gfx.render.s_attachment.iview->vk.view_type;
   }

   if (fbinfo->zs.preload.s) {
      VkImageViewType view_type =
         cmdbuf->state.gfx.render.s_attachment.iview
            ? cmdbuf->state.gfx.render.s_attachment.iview->vk.view_type
            : cmdbuf->state.gfx.render.z_attachment.iview->vk.view_type;

      key.aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
      if (!fbinfo->zs.preload.z)
         key.view_type = view_type;

      assert(key.view_type == view_type);
   }

   return cmd_emit_dcd(cmdbuf, &key);
}

static VkResult
cmd_preload_color_attachments(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   struct panvk_fb_preload_shader_key key = {
      .type = PANVK_META_OBJECT_KEY_FB_PRELOAD_SHADER,
      .samples = fbinfo->nr_samples,
      .needs_layer_id = cmdbuf->state.gfx.render.layer_count > 1,
      .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
   };
   bool needs_preload = false;

   for (uint32_t i = 0; i < fbinfo->rt_count; i++) {
      if (!fbinfo->rts[i].preload)
         continue;

      enum pipe_format pfmt = fbinfo->rts[i].view->format;
      struct panvk_image_view *iview =
         cmdbuf->state.gfx.render.color_attachments.iviews[i];

      key.color[i].type = util_format_is_pure_uint(pfmt)   ? nir_type_uint32
                          : util_format_is_pure_sint(pfmt) ? nir_type_int32
                                                           : nir_type_float32;

      if (!needs_preload) {
         key.view_type = iview->vk.view_type;
         needs_preload = true;
      }

      assert(key.view_type == iview->vk.view_type);
   }

   if (!needs_preload)
      return VK_SUCCESS;

   return cmd_emit_dcd(cmdbuf, &key);
}

VkResult
panvk_per_arch(cmd_fb_preload)(struct panvk_cmd_buffer *cmdbuf)
{
   VkResult result = cmd_preload_color_attachments(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   return cmd_preload_zs_attachments(cmdbuf);
}
