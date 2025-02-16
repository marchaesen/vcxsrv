/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "util/format/u_format.h"
#include "util/format/u_formats.h"
#include "util/u_math.h"
#include "vulkan/vulkan_core.h"
#include "agx_pack.h"
#include "hk_buffer.h"
#include "hk_cmd_buffer.h"
#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_image.h"
#include "hk_physical_device.h"

#include "layout.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_format_convert.h"
#include "shader_enums.h"
#include "vk_enum_to_str.h"
#include "vk_format.h"
#include "vk_meta.h"
#include "vk_pipeline.h"

/* For block based blit kernels, we hardcode the maximum tile size which we can
 * always achieve. This simplifies our life.
 */
#define TILE_WIDTH  32
#define TILE_HEIGHT 32

static VkResult
hk_cmd_bind_map_buffer(struct vk_command_buffer *vk_cmd,
                       struct vk_meta_device *meta, VkBuffer _buffer,
                       void **map_out)
{
   struct hk_cmd_buffer *cmd = container_of(vk_cmd, struct hk_cmd_buffer, vk);
   VK_FROM_HANDLE(hk_buffer, buffer, _buffer);

   assert(buffer->vk.size < UINT_MAX);
   struct agx_ptr T = hk_pool_alloc(cmd, buffer->vk.size, 16);
   if (unlikely(T.cpu == NULL))
      return VK_ERROR_OUT_OF_POOL_MEMORY;

   buffer->addr = T.gpu;
   *map_out = T.cpu;
   return VK_SUCCESS;
}

VkResult
hk_device_init_meta(struct hk_device *dev)
{
   VkResult result = vk_meta_device_init(&dev->vk, &dev->meta);
   if (result != VK_SUCCESS)
      return result;

   dev->meta.use_gs_for_layer = false;
   dev->meta.use_stencil_export = true;
   dev->meta.use_rect_list_pipeline = true;
   dev->meta.cmd_bind_map_buffer = hk_cmd_bind_map_buffer;
   dev->meta.max_bind_map_buffer_size_B = 64 * 1024;

   for (unsigned i = 0; i < VK_META_BUFFER_CHUNK_SIZE_COUNT; ++i) {
      dev->meta.buffer_access.optimal_wg_size[i] = 64;
   }

   return VK_SUCCESS;
}

void
hk_device_finish_meta(struct hk_device *dev)
{
   vk_meta_device_finish(&dev->vk, &dev->meta);
}

struct hk_meta_save {
   struct vk_vertex_input_state _dynamic_vi;
   struct vk_sample_locations_state _dynamic_sl;
   struct vk_dynamic_graphics_state dynamic;
   struct hk_api_shader *shaders[MESA_SHADER_MESH + 1];
   struct hk_addr_range vb0;
   struct hk_descriptor_set *desc0;
   bool has_push_desc0;
   enum agx_visibility_mode occlusion;
   struct hk_push_descriptor_set push_desc0;
   VkQueryPipelineStatisticFlags pipeline_stats_flags;
   uint8_t push[HK_MAX_PUSH_SIZE];
};

static void
hk_meta_begin(struct hk_cmd_buffer *cmd, struct hk_meta_save *save,
              VkPipelineBindPoint bind_point)
{
   struct hk_descriptor_state *desc = hk_get_descriptors_state(cmd, bind_point);

   if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      save->dynamic = cmd->vk.dynamic_graphics_state;
      save->_dynamic_vi = cmd->state.gfx._dynamic_vi;
      save->_dynamic_sl = cmd->state.gfx._dynamic_sl;

      static_assert(sizeof(cmd->state.gfx.shaders) == sizeof(save->shaders));
      memcpy(save->shaders, cmd->state.gfx.shaders, sizeof(save->shaders));

      /* Pause queries */
      save->occlusion = cmd->state.gfx.occlusion.mode;
      cmd->state.gfx.occlusion.mode = AGX_VISIBILITY_MODE_NONE;
      cmd->state.gfx.dirty |= HK_DIRTY_OCCLUSION;

      save->pipeline_stats_flags = desc->root.draw.pipeline_stats_flags;
      desc->root.draw.pipeline_stats_flags = 0;
      desc->root_dirty = true;
   } else {
      save->shaders[MESA_SHADER_COMPUTE] = cmd->state.cs.shader;
   }

   save->vb0 = cmd->state.gfx.vb[0];

   save->desc0 = desc->sets[0];
   save->has_push_desc0 = desc->push[0];
   if (save->has_push_desc0)
      save->push_desc0 = *desc->push[0];

   static_assert(sizeof(save->push) == sizeof(desc->root.push));
   memcpy(save->push, desc->root.push, sizeof(save->push));

   cmd->in_meta = true;
}

static void
hk_meta_init_render(struct hk_cmd_buffer *cmd,
                    struct vk_meta_rendering_info *info)
{
   const struct hk_rendering_state *render = &cmd->state.gfx.render;

   *info = (struct vk_meta_rendering_info){
      .samples = MAX2(render->tilebuffer.nr_samples, 1),
      .view_mask = render->view_mask,
      .color_attachment_count = render->color_att_count,
      .depth_attachment_format = render->depth_att.vk_format,
      .stencil_attachment_format = render->stencil_att.vk_format,
   };
   for (uint32_t a = 0; a < render->color_att_count; a++) {
      info->color_attachment_formats[a] = render->color_att[a].vk_format;
      info->color_attachment_write_masks[a] =
         VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   }
}

static void
hk_meta_end(struct hk_cmd_buffer *cmd, struct hk_meta_save *save,
            VkPipelineBindPoint bind_point)
{
   struct hk_descriptor_state *desc = hk_get_descriptors_state(cmd, bind_point);
   desc->root_dirty = true;

   if (save->desc0) {
      desc->sets[0] = save->desc0;
      desc->root.sets[0] = hk_descriptor_set_addr(save->desc0);
      desc->sets_dirty |= BITFIELD_BIT(0);
      desc->push_dirty &= ~BITFIELD_BIT(0);
   } else if (save->has_push_desc0) {
      *desc->push[0] = save->push_desc0;
      desc->push_dirty |= BITFIELD_BIT(0);
   }

   if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      /* Restore the dynamic state */
      assert(save->dynamic.vi == &cmd->state.gfx._dynamic_vi);
      assert(save->dynamic.ms.sample_locations == &cmd->state.gfx._dynamic_sl);
      cmd->vk.dynamic_graphics_state = save->dynamic;
      cmd->state.gfx._dynamic_vi = save->_dynamic_vi;
      cmd->state.gfx._dynamic_sl = save->_dynamic_sl;
      memcpy(cmd->vk.dynamic_graphics_state.dirty,
             cmd->vk.dynamic_graphics_state.set,
             sizeof(cmd->vk.dynamic_graphics_state.set));

      for (uint32_t stage = 0; stage < ARRAY_SIZE(save->shaders); stage++) {
         hk_cmd_bind_graphics_shader(cmd, stage, save->shaders[stage]);
      }

      hk_cmd_bind_vertex_buffer(cmd, 0, save->vb0);

      /* Restore queries */
      cmd->state.gfx.occlusion.mode = save->occlusion;
      cmd->state.gfx.dirty |= HK_DIRTY_OCCLUSION;

      desc->root.draw.pipeline_stats_flags = save->pipeline_stats_flags;
      desc->root_dirty = true;
   } else {
      hk_cmd_bind_compute_shader(cmd, save->shaders[MESA_SHADER_COMPUTE]);
   }

   memcpy(desc->root.push, save->push, sizeof(save->push));
   cmd->in_meta = false;
}

#define BINDING_OUTPUT 0
#define BINDING_INPUT  1

static VkFormat
aspect_format(VkFormat fmt, VkImageAspectFlags aspect)
{
   bool depth = (aspect & VK_IMAGE_ASPECT_DEPTH_BIT);
   bool stencil = (aspect & VK_IMAGE_ASPECT_STENCIL_BIT);

   enum pipe_format p_format = hk_format_to_pipe_format(fmt);

   if (util_format_is_depth_or_stencil(p_format)) {
      assert(depth ^ stencil);
      if (depth) {
         switch (fmt) {
         case VK_FORMAT_D32_SFLOAT:
         case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_FORMAT_D32_SFLOAT;
         case VK_FORMAT_D16_UNORM:
         case VK_FORMAT_D16_UNORM_S8_UINT:
            return VK_FORMAT_D16_UNORM;
         default:
            unreachable("invalid depth");
         }
      } else {
         switch (fmt) {
         case VK_FORMAT_S8_UINT:
         case VK_FORMAT_D32_SFLOAT_S8_UINT:
         case VK_FORMAT_D16_UNORM_S8_UINT:
            return VK_FORMAT_S8_UINT;
         default:
            unreachable("invalid stencil");
         }
      }
   }

   assert(!depth && !stencil);

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(fmt);

   if (ycbcr_info) {
      switch (aspect) {
      case VK_IMAGE_ASPECT_PLANE_0_BIT:
         return ycbcr_info->planes[0].format;
      case VK_IMAGE_ASPECT_PLANE_1_BIT:
         return ycbcr_info->planes[1].format;
      case VK_IMAGE_ASPECT_PLANE_2_BIT:
         return ycbcr_info->planes[2].format;
      default:
         unreachable("invalid ycbcr aspect");
      }
   }

   return fmt;
}

/*
 * Canonicalize formats to simplify the copies. The returned format must in the
 * same compression class, and should roundtrip lossless (minifloat formats are
 * the unfortunate exception).
 */
static enum pipe_format
canonical_format_pipe(enum pipe_format fmt, bool canonicalize_zs)
{
   if (!canonicalize_zs && util_format_is_depth_or_stencil(fmt))
      return fmt;

   assert(ail_is_valid_pixel_format(fmt));

   if (util_format_is_compressed(fmt)) {
      unsigned size_B = util_format_get_blocksize(fmt);
      assert(size_B == 8 || size_B == 16);

      return size_B == 16 ? PIPE_FORMAT_R32G32B32A32_UINT
                          : PIPE_FORMAT_R32G32_UINT;
   }

#define CASE(x, y) [AGX_CHANNELS_##x] = PIPE_FORMAT_##y
   /* clang-format off */
   static enum pipe_format map[] = {
      CASE(R8,           R8_UINT),
      CASE(R16,          R16_UNORM /* XXX: Hack for Z16 copies */),
      CASE(R8G8,         R8G8_UINT),
      CASE(R5G6B5,       R5G6B5_UNORM),
      CASE(R4G4B4A4,     R4G4B4A4_UNORM),
      CASE(A1R5G5B5,     A1R5G5B5_UNORM),
      CASE(R5G5B5A1,     B5G5R5A1_UNORM),
      CASE(R32,          R32_UINT),
      CASE(R16G16,       R16G16_UINT),
      CASE(R11G11B10,    R11G11B10_FLOAT),
      CASE(R10G10B10A2,  R10G10B10A2_UNORM),
      CASE(R9G9B9E5,     R9G9B9E5_FLOAT),
      CASE(R8G8B8A8,     R8G8B8A8_UINT),
      CASE(R32G32,       R32G32_UINT),
      CASE(R16G16B16A16, R16G16B16A16_UINT),
      CASE(R32G32B32A32, R32G32B32A32_UINT),
   };
   /* clang-format on */
#undef CASE

   enum agx_channels channels = ail_pixel_format[fmt].channels;
   assert(channels < ARRAY_SIZE(map) && "all valid channels handled");
   assert(map[channels] != PIPE_FORMAT_NONE && "all valid channels handled");
   return map[channels];
}

static VkFormat
canonical_format(VkFormat fmt)
{
   return vk_format_from_pipe_format(
      canonical_format_pipe(hk_format_to_pipe_format(fmt), false));
}

enum copy_type {
   BUF2IMG,
   IMG2BUF,
   IMG2IMG,
};

struct vk_meta_push_data {
   uint64_t buffer;

   uint32_t row_extent;
   uint32_t slice_or_layer_extent;

   int32_t src_offset_el[4];
   int32_t dst_offset_el[4];
   uint32_t grid_el[3];
} PACKED;

#define get_push(b, name)                                                      \
   nir_load_push_constant(                                                     \
      b, 1, sizeof(((struct vk_meta_push_data *)0)->name) * 8,                 \
      nir_imm_int(b, offsetof(struct vk_meta_push_data, name)))

struct vk_meta_image_copy_key {
   enum vk_meta_object_key_type key_type;
   enum copy_type type;
   enum pipe_format src_format, dst_format;
   unsigned block_size;
   unsigned nr_samples;
   bool block_based;
};

static nir_def *
linearize_coords(nir_builder *b, nir_def *coord,
                 const struct vk_meta_image_copy_key *key)
{
   assert(key->nr_samples == 1 && "buffer<-->image copies not multisampled");

   nir_def *row_extent = get_push(b, row_extent);
   nir_def *slice_or_layer_extent = get_push(b, slice_or_layer_extent);
   nir_def *x = nir_channel(b, coord, 0);
   nir_def *y = nir_channel(b, coord, 1);
   nir_def *z_or_layer = nir_channel(b, coord, 2);

   nir_def *v = nir_imul_imm(b, x, key->block_size);

   v = nir_iadd(b, v, nir_imul(b, y, row_extent));
   v = nir_iadd(b, v, nir_imul(b, z_or_layer, slice_or_layer_extent));

   return nir_udiv_imm(b, v, key->block_size);
}

static bool
is_format_native(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R8_UINT:
   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R32G32_UINT:
   case PIPE_FORMAT_R16G16_UINT:
   case PIPE_FORMAT_R16_UNORM:
      /* TODO: debug me .. why do these fail */
      return false;
   case PIPE_FORMAT_R11G11B10_FLOAT:
   case PIPE_FORMAT_R9G9B9E5_FLOAT:
   case PIPE_FORMAT_R16G16B16A16_UINT:
   case PIPE_FORMAT_R32G32B32A32_UINT:
   case PIPE_FORMAT_R8G8B8A8_UINT:
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return true;
   case PIPE_FORMAT_R5G6B5_UNORM:
   case PIPE_FORMAT_R4G4B4A4_UNORM:
   case PIPE_FORMAT_A1R5G5B5_UNORM:
   case PIPE_FORMAT_B5G5R5A1_UNORM:
      return false;
   default:
      unreachable("expected canonical");
   }
}

static nir_def *
load_store_formatted(nir_builder *b, nir_def *base, nir_def *index,
                     nir_def *value, enum pipe_format format)
{
   if (util_format_is_depth_or_stencil(format))
      format = canonical_format_pipe(format, true);

   if (is_format_native(format)) {
      enum pipe_format isa = ail_pixel_format[format].renderable;
      unsigned isa_size = util_format_get_blocksize(isa);
      unsigned isa_components = util_format_get_blocksize(format) / isa_size;
      unsigned shift = util_logbase2(isa_components);

      if (value) {
         nir_store_agx(b, value, base, index, .format = isa, .base = shift);
      } else {
         return nir_load_agx(b, 4, 32, base, index, .format = isa,
                             .base = shift);
      }
   } else {
      unsigned blocksize_B = util_format_get_blocksize(format);
      nir_def *addr =
         nir_iadd(b, base, nir_imul_imm(b, nir_u2u64(b, index), blocksize_B));

      if (value) {
         nir_def *raw = nir_format_pack_rgba(b, format, value);

         if (blocksize_B <= 4) {
            assert(raw->num_components == 1);
            raw = nir_u2uN(b, raw, blocksize_B * 8);
         } else {
            assert(raw->bit_size == 32);
            raw = nir_trim_vector(b, raw, blocksize_B / 4);
         }

         nir_store_global(b, addr, blocksize_B, raw,
                          nir_component_mask(raw->num_components));
      } else {
         nir_def *raw =
            nir_load_global(b, addr, blocksize_B, DIV_ROUND_UP(blocksize_B, 4),
                            MIN2(32, blocksize_B * 8));

         return nir_format_unpack_rgba(b, raw, format);
      }
   }

   return NULL;
}

static nir_shader *
build_image_copy_shader(const struct vk_meta_image_copy_key *key)
{
   nir_builder build =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "hk-meta-copy");

   nir_builder *b = &build;
   b->shader->info.workgroup_size[0] = TILE_WIDTH;
   b->shader->info.workgroup_size[1] = TILE_HEIGHT;

   bool src_is_buf = key->type == BUF2IMG;
   bool dst_is_buf = key->type == IMG2BUF;

   bool msaa = key->nr_samples > 1;
   enum glsl_sampler_dim dim_2d =
      msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;
   enum glsl_sampler_dim dim_src = src_is_buf ? GLSL_SAMPLER_DIM_BUF : dim_2d;
   enum glsl_sampler_dim dim_dst = dst_is_buf ? GLSL_SAMPLER_DIM_BUF : dim_2d;

   const struct glsl_type *texture_type =
      glsl_sampler_type(dim_src, false, !src_is_buf, GLSL_TYPE_UINT);

   const struct glsl_type *image_type =
      glsl_image_type(dim_dst, !dst_is_buf, GLSL_TYPE_UINT);

   nir_variable *texture =
      nir_variable_create(b->shader, nir_var_uniform, texture_type, "source");
   nir_variable *image =
      nir_variable_create(b->shader, nir_var_image, image_type, "dest");

   image->data.descriptor_set = 0;
   image->data.binding = BINDING_OUTPUT;
   image->data.access = ACCESS_NON_READABLE;

   texture->data.descriptor_set = 0;
   texture->data.binding = BINDING_INPUT;

   /* Grab the offset vectors */
   nir_def *src_offset_el = nir_load_push_constant(
      b, 3, 32,
      nir_imm_int(b, offsetof(struct vk_meta_push_data, src_offset_el)));

   nir_def *dst_offset_el = nir_load_push_constant(
      b, 3, 32,
      nir_imm_int(b, offsetof(struct vk_meta_push_data, dst_offset_el)));

   nir_def *grid_2d_el = nir_load_push_constant(
      b, 2, 32, nir_imm_int(b, offsetof(struct vk_meta_push_data, grid_el)));

   /* We're done setting up variables, do the copy */
   nir_def *coord = nir_load_global_invocation_id(b, 32);

   /* The destination format is already canonical, convert to an ISA format */
   enum pipe_format isa_format = PIPE_FORMAT_NONE;
   if (key->block_based) {
      enum pipe_format pipe = canonical_format_pipe(key->dst_format, true);
      isa_format = ail_pixel_format[pipe].renderable;
      assert(isa_format != PIPE_FORMAT_NONE);
   }

   nir_def *local_offset = nir_imm_intN_t(b, 0, 16);
   nir_def *lid = nir_trim_vector(b, nir_load_local_invocation_id(b), 2);
   lid = nir_u2u16(b, lid);

   nir_def *src_coord = src_is_buf ? coord : nir_iadd(b, coord, src_offset_el);
   nir_def *dst_coord = dst_is_buf ? coord : nir_iadd(b, coord, dst_offset_el);

   nir_def *image_deref = &nir_build_deref_var(b, image)->def;

   nir_def *coord_2d_el = nir_trim_vector(b, coord, 2);
   nir_def *in_bounds;
   if (key->block_based) {
      nir_def *offset_in_block_el =
         nir_umod_imm(b, nir_trim_vector(b, dst_offset_el, 2), TILE_WIDTH);

      dst_coord =
         nir_vector_insert_imm(b, nir_isub(b, dst_coord, offset_in_block_el),
                               nir_channel(b, dst_coord, 2), 2);

      src_coord =
         nir_vector_insert_imm(b, nir_isub(b, src_coord, offset_in_block_el),
                               nir_channel(b, src_coord, 2), 2);

      in_bounds = nir_uge(b, coord_2d_el, offset_in_block_el);
      in_bounds = nir_iand(
         b, in_bounds,
         nir_ult(b, coord_2d_el, nir_iadd(b, offset_in_block_el, grid_2d_el)));
   } else {
      in_bounds = nir_ult(b, coord_2d_el, grid_2d_el);
   }

   /* Special case handle buffer indexing */
   if (dst_is_buf) {
      assert(!key->block_based);
      dst_coord = linearize_coords(b, dst_coord, key);
   } else if (src_is_buf) {
      src_coord = linearize_coords(b, src_coord, key);
   }

   for (unsigned s = 0; s < key->nr_samples; ++s) {
      nir_def *ms_index = nir_imm_int(b, s);
      nir_def *value1 = NULL, *value2 = NULL;

      nir_push_if(b, nir_ball(b, in_bounds));
      {
         /* Copy formatted texel from texture to storage image */
         nir_deref_instr *deref = nir_build_deref_var(b, texture);

         if (src_is_buf) {
            value1 = load_store_formatted(b, get_push(b, buffer), src_coord,
                                          NULL, key->dst_format);
         } else {
            if (msaa) {
               value1 = nir_txf_ms_deref(b, deref, src_coord, ms_index);
            } else {
               value1 = nir_txf_deref(b, deref, src_coord, NULL);
            }

            /* Munge according to the implicit conversions so we get a bit copy */
            if (key->src_format != key->dst_format) {
               nir_def *packed =
                  nir_format_pack_rgba(b, key->src_format, value1);

               value1 = nir_format_unpack_rgba(b, packed, key->dst_format);
            }
         }

         if (dst_is_buf) {
            load_store_formatted(b, get_push(b, buffer), dst_coord, value1,
                                 key->dst_format);
         } else if (!key->block_based) {
            nir_image_deref_store(b, image_deref, nir_pad_vec4(b, dst_coord),
                                  ms_index, value1, nir_imm_int(b, 0),
                                  .image_dim = dim_dst,
                                  .image_array = !dst_is_buf);
         }
      }
      nir_push_else(b, NULL);
      if (key->block_based) {
         /* Copy back the existing destination content */
         value2 = nir_image_deref_load(b, 4, 32, image_deref,
                                       nir_pad_vec4(b, dst_coord), ms_index,
                                       nir_imm_int(b, 0), .image_dim = dim_dst,
                                       .image_array = !dst_is_buf);
      }
      nir_pop_if(b, NULL);

      if (key->block_based) {
         /* Must define the phi first so we validate. */
         nir_def *phi = nir_if_phi(b, value1, value2);
         nir_def *mask = nir_imm_intN_t(b, 1 << s, 16);

         nir_store_local_pixel_agx(b, phi, mask, lid, .base = 0,
                                   .write_mask = 0xf, .format = isa_format,
                                   .explicit_coord = true);
      }
   }

   if (key->block_based) {
      assert(!dst_is_buf);

      nir_barrier(b, .execution_scope = SCOPE_WORKGROUP);

      nir_push_if(b, nir_ball(b, nir_ieq_imm(b, lid, 0)));
      {
         nir_image_deref_store_block_agx(
            b, image_deref, local_offset, dst_coord, .format = isa_format,
            .image_dim = dim_2d, .image_array = true, .explicit_coord = true);
      }
      nir_pop_if(b, NULL);
      b->shader->info.cs.image_block_size_per_thread_agx =
         util_format_get_blocksize(key->dst_format);
   }

   return b->shader;
}

static VkResult
get_image_copy_descriptor_set_layout(struct vk_device *device,
                                     struct vk_meta_device *meta,
                                     VkDescriptorSetLayout *layout_out,
                                     enum copy_type type)
{
   const char *keys[] = {
      [IMG2BUF] = "vk-meta-copy-image-to-buffer-descriptor-set-layout",
      [BUF2IMG] = "vk-meta-copy-buffer-to-image-descriptor-set-layout",
      [IMG2IMG] = "vk-meta-copy-image-to-image-descriptor-set-layout",
   };

   VkDescriptorSetLayout from_cache = vk_meta_lookup_descriptor_set_layout(
      meta, keys[type], strlen(keys[type]));
   if (from_cache != VK_NULL_HANDLE) {
      *layout_out = from_cache;
      return VK_SUCCESS;
   }

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = BINDING_OUTPUT,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = BINDING_INPUT,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
   };

   const VkDescriptorSetLayoutCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = ARRAY_SIZE(bindings),
      .pBindings = bindings,
   };

   return vk_meta_create_descriptor_set_layout(device, meta, &info, keys[type],
                                               strlen(keys[type]), layout_out);
}

static VkResult
get_image_copy_pipeline_layout(struct vk_device *device,
                               struct vk_meta_device *meta,
                               struct vk_meta_image_copy_key *key,
                               VkDescriptorSetLayout set_layout,
                               VkPipelineLayout *layout_out,
                               enum copy_type type)
{
   const char *keys[] = {
      [IMG2BUF] = "vk-meta-copy-image-to-buffer-pipeline-layout",
      [BUF2IMG] = "vk-meta-copy-buffer-to-image-pipeline-layout",
      [IMG2IMG] = "vk-meta-copy-image-to-image-pipeline-layout",
   };

   VkPipelineLayout from_cache =
      vk_meta_lookup_pipeline_layout(meta, keys[type], strlen(keys[type]));
   if (from_cache != VK_NULL_HANDLE) {
      *layout_out = from_cache;
      return VK_SUCCESS;
   }

   VkPipelineLayoutCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &set_layout,
   };

   const VkPushConstantRange push_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(struct vk_meta_push_data),
   };

   info.pushConstantRangeCount = 1;
   info.pPushConstantRanges = &push_range;

   return vk_meta_create_pipeline_layout(device, meta, &info, keys[type],
                                         strlen(keys[type]), layout_out);
}

static VkResult
get_image_copy_pipeline(struct vk_device *device, struct vk_meta_device *meta,
                        const struct vk_meta_image_copy_key *key,
                        VkPipelineLayout layout, VkPipeline *pipeline_out)
{
   VkPipeline from_cache = vk_meta_lookup_pipeline(meta, key, sizeof(*key));
   if (from_cache != VK_NULL_HANDLE) {
      *pipeline_out = from_cache;
      return VK_SUCCESS;
   }

   const VkPipelineShaderStageNirCreateInfoMESA nir_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NIR_CREATE_INFO_MESA,
      .nir = build_image_copy_shader(key),
   };
   const VkPipelineShaderStageCreateInfo cs_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = &nir_info,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .pName = "main",
   };

   const VkComputePipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = cs_info,
      .layout = layout,
   };

   VkResult result = vk_meta_create_compute_pipeline(
      device, meta, &info, key, sizeof(*key), pipeline_out);
   ralloc_free(nir_info.nir);

   return result;
}

static void
hk_meta_copy_image_to_buffer2(struct vk_command_buffer *cmd,
                              struct vk_meta_device *meta,
                              const VkCopyImageToBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(vk_image, image, pCopyBufferInfo->srcImage);
   VK_FROM_HANDLE(vk_image, src_image, pCopyBufferInfo->srcImage);
   VK_FROM_HANDLE(hk_buffer, buffer, pCopyBufferInfo->dstBuffer);

   struct vk_device *device = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &device->dispatch_table;

   VkResult result;

   VkDescriptorSetLayout set_layout;
   result =
      get_image_copy_descriptor_set_layout(device, meta, &set_layout, IMG2BUF);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   bool per_layer =
      util_format_is_compressed(hk_format_to_pipe_format(image->format));

   for (unsigned i = 0; i < pCopyBufferInfo->regionCount; ++i) {
      const VkBufferImageCopy2 *region = &pCopyBufferInfo->pRegions[i];

      unsigned layers = MAX2(region->imageExtent.depth,
                             vk_image_subresource_layer_count(
                                src_image, &region->imageSubresource));
      unsigned layer_iters = per_layer ? layers : 1;

      for (unsigned layer_offs = 0; layer_offs < layer_iters; ++layer_offs) {

         VkImageAspectFlags aspect = region->imageSubresource.aspectMask;
         VkFormat aspect_fmt = aspect_format(image->format, aspect);
         VkFormat canonical = canonical_format(aspect_fmt);

         uint32_t blocksize_B =
            util_format_get_blocksize(hk_format_to_pipe_format(canonical));

         enum pipe_format p_format = hk_format_to_pipe_format(image->format);

         unsigned row_extent = util_format_get_nblocksx(
                                  p_format, MAX2(region->bufferRowLength,
                                                 region->imageExtent.width)) *
                               blocksize_B;
         unsigned slice_extent =
            util_format_get_nblocksy(
               p_format,
               MAX2(region->bufferImageHeight, region->imageExtent.height)) *
            row_extent;
         unsigned layer_extent =
            util_format_get_nblocksz(p_format, region->imageExtent.depth) *
            slice_extent;

         bool is_3d = region->imageExtent.depth > 1;

         struct vk_meta_image_copy_key key = {
            .key_type = VK_META_OBJECT_KEY_COPY_IMAGE_TO_BUFFER,
            .type = IMG2BUF,
            .block_size = blocksize_B,
            .nr_samples = image->samples,
            .src_format = hk_format_to_pipe_format(canonical),
            .dst_format = hk_format_to_pipe_format(canonical),
         };

         VkPipelineLayout pipeline_layout;
         result = get_image_copy_pipeline_layout(device, meta, &key, set_layout,
                                                 &pipeline_layout, false);
         if (unlikely(result != VK_SUCCESS)) {
            vk_command_buffer_set_error(cmd, result);
            return;
         }

         VkImageView src_view;
         const VkImageViewUsageCreateInfo src_view_usage = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
         };
         const VkImageViewCreateInfo src_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
            .pNext = &src_view_usage,
            .image = pCopyBufferInfo->srcImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = canonical,
            .subresourceRange =
               {
                  .aspectMask = region->imageSubresource.aspectMask,
                  .baseMipLevel = region->imageSubresource.mipLevel,
                  .baseArrayLayer =
                     MAX2(region->imageOffset.z,
                          region->imageSubresource.baseArrayLayer) +
                     layer_offs,
                  .layerCount = per_layer ? 1 : layers,
                  .levelCount = 1,
               },
         };

         result =
            vk_meta_create_image_view(cmd, meta, &src_view_info, &src_view);
         if (unlikely(result != VK_SUCCESS)) {
            vk_command_buffer_set_error(cmd, result);
            return;
         }

         VkDescriptorImageInfo src_info = {
            .imageLayout = pCopyBufferInfo->srcImageLayout,
            .imageView = src_view,
         };

         VkWriteDescriptorSet desc_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = 0,
            .dstBinding = BINDING_INPUT,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .pImageInfo = &src_info,
         };

         disp->CmdPushDescriptorSetKHR(vk_command_buffer_to_handle(cmd),
                                       VK_PIPELINE_BIND_POINT_COMPUTE,
                                       pipeline_layout, 0, 1, &desc_write);

         VkPipeline pipeline;
         result = get_image_copy_pipeline(device, meta, &key, pipeline_layout,
                                          &pipeline);
         if (unlikely(result != VK_SUCCESS)) {
            vk_command_buffer_set_error(cmd, result);
            return;
         }

         disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                               VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

         enum pipe_format p_src_fmt =
            hk_format_to_pipe_format(src_image->format);

         struct vk_meta_push_data push = {
            .buffer = hk_buffer_address(buffer, region->bufferOffset),
            .row_extent = row_extent,
            .slice_or_layer_extent = is_3d ? slice_extent : layer_extent,

            .src_offset_el[0] =
               util_format_get_nblocksx(p_src_fmt, region->imageOffset.x),
            .src_offset_el[1] =
               util_format_get_nblocksy(p_src_fmt, region->imageOffset.y),

            .grid_el[0] =
               util_format_get_nblocksx(p_format, region->imageExtent.width),
            .grid_el[1] =
               util_format_get_nblocksy(p_format, region->imageExtent.height),
            .grid_el[2] = per_layer ? 1 : layers,
         };

         push.buffer += push.slice_or_layer_extent * layer_offs;

         disp->CmdPushConstants(vk_command_buffer_to_handle(cmd),
                                pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(push), &push);

         disp->CmdDispatch(vk_command_buffer_to_handle(cmd),
                           DIV_ROUND_UP(push.grid_el[0], 32),
                           DIV_ROUND_UP(push.grid_el[1], 32), push.grid_el[2]);
      }
   }
}

static void
hk_meta_dispatch_to_image(struct vk_command_buffer *cmd,
                          const struct vk_device_dispatch_table *disp,
                          VkPipelineLayout pipeline_layout,
                          struct vk_meta_push_data *push, VkOffset3D offset,
                          VkExtent3D extent, bool per_layer, unsigned layers,
                          enum pipe_format p_dst_fmt, enum pipe_format p_format)
{
   push->dst_offset_el[0] = util_format_get_nblocksx(p_dst_fmt, offset.x);
   push->dst_offset_el[1] = util_format_get_nblocksy(p_dst_fmt, offset.y);
   push->dst_offset_el[2] = 0;

   push->grid_el[0] = util_format_get_nblocksx(p_format, extent.width);
   push->grid_el[1] = util_format_get_nblocksy(p_format, extent.height);
   push->grid_el[2] = per_layer ? 1 : layers;

   unsigned w_el = util_format_get_nblocksx(p_format, extent.width);
   unsigned h_el = util_format_get_nblocksy(p_format, extent.height);

   /* Expand the grid so destinations are in tiles */
   unsigned expanded_x0 = push->dst_offset_el[0] & ~(TILE_WIDTH - 1);
   unsigned expanded_y0 = push->dst_offset_el[1] & ~(TILE_HEIGHT - 1);
   unsigned expanded_x1 = align(push->dst_offset_el[0] + w_el, TILE_WIDTH);
   unsigned expanded_y1 = align(push->dst_offset_el[1] + h_el, TILE_HEIGHT);

   /* TODO: clamp to the destination size to save some redundant threads? */

   disp->CmdPushConstants(vk_command_buffer_to_handle(cmd), pipeline_layout,
                          VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(*push), push);

   disp->CmdDispatch(vk_command_buffer_to_handle(cmd),
                     (expanded_x1 - expanded_x0) / TILE_WIDTH,
                     (expanded_y1 - expanded_y0) / TILE_HEIGHT,
                     push->grid_el[2]);
}

static void
hk_meta_copy_buffer_to_image2(struct vk_command_buffer *cmd,
                              struct vk_meta_device *meta,
                              const struct VkCopyBufferToImageInfo2 *info)
{
   VK_FROM_HANDLE(vk_image, image, info->dstImage);
   VK_FROM_HANDLE(hk_buffer, buffer, info->srcBuffer);

   struct vk_device *device = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &device->dispatch_table;

   VkDescriptorSetLayout set_layout;
   VkResult result =
      get_image_copy_descriptor_set_layout(device, meta, &set_layout, BUF2IMG);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   bool per_layer =
      util_format_is_compressed(hk_format_to_pipe_format(image->format));

   for (unsigned r = 0; r < info->regionCount; ++r) {
      const VkBufferImageCopy2 *region = &info->pRegions[r];

      unsigned layers = MAX2(
         region->imageExtent.depth,
         vk_image_subresource_layer_count(image, &region->imageSubresource));
      unsigned layer_iters = per_layer ? layers : 1;

      for (unsigned layer_offs = 0; layer_offs < layer_iters; ++layer_offs) {
         VkImageAspectFlags aspect = region->imageSubresource.aspectMask;
         VkFormat aspect_fmt = aspect_format(image->format, aspect);
         VkFormat canonical = canonical_format(aspect_fmt);
         enum pipe_format p_format = hk_format_to_pipe_format(aspect_fmt);
         uint32_t blocksize_B = util_format_get_blocksize(p_format);
         bool is_3d = region->imageExtent.depth > 1;

         struct vk_meta_image_copy_key key = {
            .key_type = VK_META_OBJECT_KEY_COPY_IMAGE_TO_BUFFER,
            .type = BUF2IMG,
            .block_size = blocksize_B,
            .nr_samples = image->samples,
            .src_format = hk_format_to_pipe_format(canonical),
            .dst_format = canonical_format_pipe(
               hk_format_to_pipe_format(aspect_format(image->format, aspect)),
               false),

            /* TODO: MSAA path */
            .block_based =
               (image->image_type != VK_IMAGE_TYPE_1D) && image->samples == 1,
         };

         VkPipelineLayout pipeline_layout;
         result = get_image_copy_pipeline_layout(device, meta, &key, set_layout,
                                                 &pipeline_layout, true);
         if (unlikely(result != VK_SUCCESS)) {
            vk_command_buffer_set_error(cmd, result);
            return;
         }

         unsigned row_extent = util_format_get_nblocksx(
                                  p_format, MAX2(region->bufferRowLength,
                                                 region->imageExtent.width)) *
                               blocksize_B;
         unsigned slice_extent =
            util_format_get_nblocksy(
               p_format,
               MAX2(region->bufferImageHeight, region->imageExtent.height)) *
            row_extent;
         unsigned layer_extent =
            util_format_get_nblocksz(p_format, region->imageExtent.depth) *
            slice_extent;

         VkImageView dst_view;
         const VkImageViewUsageCreateInfo dst_view_usage = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT,
         };
         const VkImageViewCreateInfo dst_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
            .pNext = &dst_view_usage,
            .image = info->dstImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = canonical,
            .subresourceRange =
               {
                  .aspectMask = region->imageSubresource.aspectMask,
                  .baseMipLevel = region->imageSubresource.mipLevel,
                  .baseArrayLayer =
                     MAX2(region->imageOffset.z,
                          region->imageSubresource.baseArrayLayer) +
                     layer_offs,
                  .layerCount = per_layer ? 1 : layers,
                  .levelCount = 1,
               },
         };

         result =
            vk_meta_create_image_view(cmd, meta, &dst_view_info, &dst_view);
         if (unlikely(result != VK_SUCCESS)) {
            vk_command_buffer_set_error(cmd, result);
            return;
         }

         const VkDescriptorImageInfo dst_info = {
            .imageView = dst_view,
            .imageLayout = info->dstImageLayout,
         };

         VkWriteDescriptorSet desc_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = 0,
            .dstBinding = BINDING_OUTPUT,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .pImageInfo = &dst_info,
         };

         disp->CmdPushDescriptorSetKHR(vk_command_buffer_to_handle(cmd),
                                       VK_PIPELINE_BIND_POINT_COMPUTE,
                                       pipeline_layout, 0, 1, &desc_write);

         VkPipeline pipeline;
         result = get_image_copy_pipeline(device, meta, &key, pipeline_layout,
                                          &pipeline);
         if (unlikely(result != VK_SUCCESS)) {
            vk_command_buffer_set_error(cmd, result);
            return;
         }

         disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                               VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

         struct vk_meta_push_data push = {
            .buffer = hk_buffer_address(buffer, region->bufferOffset),
            .row_extent = row_extent,
            .slice_or_layer_extent = is_3d ? slice_extent : layer_extent,
         };

         push.buffer += push.slice_or_layer_extent * layer_offs;

         hk_meta_dispatch_to_image(cmd, disp, pipeline_layout, &push,
                                   region->imageOffset, region->imageExtent,
                                   per_layer, layers, p_format, p_format);
      }
   }
}

static void
hk_meta_copy_image2(struct vk_command_buffer *cmd, struct vk_meta_device *meta,
                    const struct VkCopyImageInfo2 *info)
{
   VK_FROM_HANDLE(vk_image, src_image, info->srcImage);
   VK_FROM_HANDLE(vk_image, dst_image, info->dstImage);

   struct vk_device *device = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &device->dispatch_table;

   VkDescriptorSetLayout set_layout;
   VkResult result =
      get_image_copy_descriptor_set_layout(device, meta, &set_layout, BUF2IMG);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   bool per_layer =
      util_format_is_compressed(hk_format_to_pipe_format(src_image->format)) ||
      util_format_is_compressed(hk_format_to_pipe_format(dst_image->format));

   for (unsigned r = 0; r < info->regionCount; ++r) {
      const VkImageCopy2 *region = &info->pRegions[r];

      unsigned layers = MAX2(
         vk_image_subresource_layer_count(src_image, &region->srcSubresource),
         region->extent.depth);
      unsigned layer_iters = per_layer ? layers : 1;

      for (unsigned layer_offs = 0; layer_offs < layer_iters; ++layer_offs) {
         u_foreach_bit(aspect, region->srcSubresource.aspectMask) {
            /* We use the source format throughout for consistent scaling with
             * compressed<-->uncompressed copies, where the extents are defined
             * to follow the source.
             */
            VkFormat aspect_fmt = aspect_format(src_image->format, 1 << aspect);
            VkFormat canonical = canonical_format(aspect_fmt);
            uint32_t blocksize_B =
               util_format_get_blocksize(hk_format_to_pipe_format(canonical));

            VkImageAspectFlagBits dst_aspect_mask =
               vk_format_get_ycbcr_info(dst_image->format) ||
                     vk_format_get_ycbcr_info(src_image->format)
                  ? region->dstSubresource.aspectMask
                  : (1 << aspect);

            struct vk_meta_image_copy_key key = {
               .key_type = VK_META_OBJECT_KEY_COPY_IMAGE_TO_BUFFER,
               .type = IMG2IMG,
               .block_size = blocksize_B,
               .nr_samples = dst_image->samples,
               .src_format = hk_format_to_pipe_format(canonical),
               .dst_format =
                  canonical_format_pipe(hk_format_to_pipe_format(aspect_format(
                                           dst_image->format, dst_aspect_mask)),
                                        false),

               /* TODO: MSAA path */
               .block_based = (dst_image->image_type != VK_IMAGE_TYPE_1D) &&
                              dst_image->samples == 1,
            };

            assert(key.nr_samples == src_image->samples);

            VkPipelineLayout pipeline_layout;
            result = get_image_copy_pipeline_layout(
               device, meta, &key, set_layout, &pipeline_layout, true);
            if (unlikely(result != VK_SUCCESS)) {
               vk_command_buffer_set_error(cmd, result);
               return;
            }

            VkWriteDescriptorSet desc_writes[2];

            VkImageView src_view;
            const VkImageViewUsageCreateInfo src_view_usage = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
               .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            };
            const VkImageViewCreateInfo src_view_info = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
               .pNext = &src_view_usage,
               .image = info->srcImage,
               .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
               .format = canonical,
               .subresourceRange =
                  {
                     .aspectMask =
                        region->srcSubresource.aspectMask & (1 << aspect),
                     .baseMipLevel = region->srcSubresource.mipLevel,
                     .baseArrayLayer =
                        MAX2(region->srcOffset.z,
                             region->srcSubresource.baseArrayLayer) +
                        layer_offs,
                     .layerCount = per_layer ? 1 : layers,
                     .levelCount = 1,
                  },
            };

            result =
               vk_meta_create_image_view(cmd, meta, &src_view_info, &src_view);
            if (unlikely(result != VK_SUCCESS)) {
               vk_command_buffer_set_error(cmd, result);
               return;
            }

            VkDescriptorImageInfo src_info = {
               .imageLayout = info->srcImageLayout,
               .imageView = src_view,
            };

            VkImageView dst_view;
            const VkImageViewUsageCreateInfo dst_view_usage = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
               .usage = VK_IMAGE_USAGE_STORAGE_BIT,
            };
            const VkImageViewCreateInfo dst_view_info = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
               .pNext = &dst_view_usage,
               .image = info->dstImage,
               .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
               .format = vk_format_from_pipe_format(key.dst_format),
               .subresourceRange =
                  {
                     .aspectMask = dst_aspect_mask,
                     .baseMipLevel = region->dstSubresource.mipLevel,
                     .baseArrayLayer =
                        MAX2(region->dstOffset.z,
                             region->dstSubresource.baseArrayLayer) +
                        layer_offs,
                     .layerCount = per_layer ? 1 : layers,
                     .levelCount = 1,
                  },
            };

            result =
               vk_meta_create_image_view(cmd, meta, &dst_view_info, &dst_view);
            if (unlikely(result != VK_SUCCESS)) {
               vk_command_buffer_set_error(cmd, result);
               return;
            }

            const VkDescriptorImageInfo dst_info = {
               .imageView = dst_view,
               .imageLayout = info->dstImageLayout,
            };

            desc_writes[0] = (VkWriteDescriptorSet){
               .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
               .dstSet = 0,
               .dstBinding = BINDING_OUTPUT,
               .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
               .descriptorCount = 1,
               .pImageInfo = &dst_info,
            };

            desc_writes[1] = (VkWriteDescriptorSet){
               .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
               .dstSet = 0,
               .dstBinding = BINDING_INPUT,
               .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
               .descriptorCount = 1,
               .pImageInfo = &src_info,
            };

            disp->CmdPushDescriptorSetKHR(
               vk_command_buffer_to_handle(cmd), VK_PIPELINE_BIND_POINT_COMPUTE,
               pipeline_layout, 0, ARRAY_SIZE(desc_writes), desc_writes);

            VkPipeline pipeline;
            result = get_image_copy_pipeline(device, meta, &key,
                                             pipeline_layout, &pipeline);
            if (unlikely(result != VK_SUCCESS)) {
               vk_command_buffer_set_error(cmd, result);
               return;
            }

            disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                                  VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

            enum pipe_format p_src_fmt =
               hk_format_to_pipe_format(src_image->format);
            enum pipe_format p_dst_fmt =
               hk_format_to_pipe_format(dst_image->format);
            enum pipe_format p_format = hk_format_to_pipe_format(aspect_fmt);

            struct vk_meta_push_data push = {
               .src_offset_el[0] =
                  util_format_get_nblocksx(p_src_fmt, region->srcOffset.x),
               .src_offset_el[1] =
                  util_format_get_nblocksy(p_src_fmt, region->srcOffset.y),
            };

            hk_meta_dispatch_to_image(cmd, disp, pipeline_layout, &push,
                                      region->dstOffset, region->extent,
                                      per_layer, layers, p_dst_fmt, p_format);
         }
      }
   }
}

static inline struct vk_meta_copy_image_properties
hk_meta_copy_get_image_properties(struct hk_image *img)
{
   struct vk_meta_copy_image_properties props = {.tile_size = {16, 16, 1}};

   if (!vk_format_is_depth_or_stencil(img->vk.format)) {
      props.color.view_format = img->vk.format;
   } else {
      switch (img->vk.format) {
      case VK_FORMAT_S8_UINT:
         props.stencil.view_format = VK_FORMAT_R8_UINT;
         props.stencil.component_mask = BITFIELD_MASK(1);
         break;
      case VK_FORMAT_D32_SFLOAT_S8_UINT:
         props.depth.view_format = VK_FORMAT_R32G32_UINT;
         props.depth.component_mask = BITFIELD_BIT(0);
         props.stencil.view_format = VK_FORMAT_R32G32_UINT;
         props.stencil.component_mask = BITFIELD_BIT(1);
         break;
      case VK_FORMAT_D16_UNORM:
         props.depth.view_format = VK_FORMAT_R16_UINT;
         props.depth.component_mask = BITFIELD_BIT(0);
         break;
      case VK_FORMAT_D32_SFLOAT:
         props.depth.view_format = VK_FORMAT_R32_UINT;
         props.depth.component_mask = BITFIELD_BIT(0);
         break;
      default:
         unreachable("Invalid ZS format");
      }
   }

   return props;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdBlitImage2(VkCommandBuffer commandBuffer,
                 const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   perf_debug(dev, "Blit image");

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_blit_image2(&cmd->vk, &dev->meta, pBlitImageInfo);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdResolveImage2(VkCommandBuffer commandBuffer,
                    const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   perf_debug(dev, "Resolve");

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_resolve_image2(&cmd->vk, &dev->meta, pResolveImageInfo);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

void
hk_meta_resolve_rendering(struct hk_cmd_buffer *cmd,
                          const VkRenderingInfo *pRenderingInfo)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_resolve_rendering(&cmd->vk, &dev->meta, pRenderingInfo);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                  const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
   vk_meta_copy_buffer(&cmd->vk, &dev->meta, pCopyBufferInfo);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
}

static bool
hk_copy_requires_gfx(struct hk_image *img)
{
   return img->vk.samples > 1 && ail_is_compressed(&img->planes[0].layout);
}

static bool
hk_bind_point(bool gfx)
{
   return gfx ? VK_PIPELINE_BIND_POINT_GRAPHICS
              : VK_PIPELINE_BIND_POINT_COMPUTE;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                         const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_image, dst_image, pCopyBufferToImageInfo->dstImage);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   bool gfx = hk_copy_requires_gfx(dst_image);
   VkPipelineBindPoint bind_point = hk_bind_point(gfx);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, bind_point);

   if (gfx) {
      struct vk_meta_copy_image_properties dst_props =
         hk_meta_copy_get_image_properties(dst_image);

      vk_meta_copy_buffer_to_image(&cmd->vk, &dev->meta, pCopyBufferToImageInfo,
                                   &dst_props, bind_point);
   } else {
      hk_meta_copy_buffer_to_image2(&cmd->vk, &dev->meta,
                                    pCopyBufferToImageInfo);
   }

   hk_meta_end(cmd, &save, bind_point);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                         const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
   hk_meta_copy_image_to_buffer2(&cmd->vk, &dev->meta, pCopyImageToBufferInfo);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdCopyImage2(VkCommandBuffer commandBuffer,
                 const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_image, src_image, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(hk_image, dst_image, pCopyImageInfo->dstImage);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   bool gfx = hk_copy_requires_gfx(dst_image);
   VkPipelineBindPoint bind_point = hk_bind_point(gfx);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, bind_point);

   if (gfx) {
      struct vk_meta_copy_image_properties src_props =
         hk_meta_copy_get_image_properties(src_image);
      struct vk_meta_copy_image_properties dst_props =
         hk_meta_copy_get_image_properties(dst_image);

      vk_meta_copy_image(&cmd->vk, &dev->meta, pCopyImageInfo, &src_props,
                         &dst_props, bind_point);
   } else {
      hk_meta_copy_image2(&cmd->vk, &dev->meta, pCopyImageInfo);
   }

   hk_meta_end(cmd, &save, bind_point);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                 VkDeviceSize dstOffset, VkDeviceSize dstRange, uint32_t data)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
   vk_meta_fill_buffer(&cmd->vk, &dev->meta, dstBuffer, dstOffset, dstRange,
                       data);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                   VkDeviceSize dstOffset, VkDeviceSize dstRange,
                   const void *pData)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
   vk_meta_update_buffer(&cmd->vk, &dev->meta, dstBuffer, dstOffset, dstRange,
                         pData);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                       const VkClearAttachment *pAttachments,
                       uint32_t rectCount, const VkClearRect *pRects)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   struct vk_meta_rendering_info render_info;
   hk_meta_init_render(cmd, &render_info);

   struct hk_meta_save save;
   hk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_clear_attachments(&cmd->vk, &dev->meta, &render_info,
                             attachmentCount, pAttachments, rectCount, pRects);
   hk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
}
