/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dzn_private.h"

#include "spirv/nir_spirv.h"

#include "dxil_nir.h"
#include "nir_to_dxil.h"
#include "dxil_spirv_nir.h"
#include "spirv_to_dxil.h"

#include "dxil_validator.h"

#include "vk_alloc.h"
#include "vk_util.h"
#include "vk_format.h"
#include "vk_pipeline.h"
#include "vk_pipeline_cache.h"

#include "util/u_debug.h"

#define d3d12_pipeline_state_stream_new_desc(__stream, __maxstreamsz, __id, __type, __desc) \
   __type *__desc; \
   do { \
      struct { \
         D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; \
         __type desc; \
      } *__wrapper; \
      (__stream)->SizeInBytes = ALIGN_POT((__stream)->SizeInBytes, alignof(void *)); \
      __wrapper = (void *)((uint8_t *)(__stream)->pPipelineStateSubobjectStream + (__stream)->SizeInBytes); \
      (__stream)->SizeInBytes += sizeof(*__wrapper); \
      assert((__stream)->SizeInBytes <= __maxstreamsz); \
      __wrapper->type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ ## __id; \
      __desc = &__wrapper->desc; \
      memset(__desc, 0, sizeof(*__desc)); \
   } while (0)

#define d3d12_gfx_pipeline_state_stream_new_desc(__stream, __id, __type, __desc) \
   d3d12_pipeline_state_stream_new_desc(__stream, MAX_GFX_PIPELINE_STATE_STREAM_SIZE, __id, __type, __desc)

#define d3d12_compute_pipeline_state_stream_new_desc(__stream, __id, __type, __desc) \
   d3d12_pipeline_state_stream_new_desc(__stream, MAX_COMPUTE_PIPELINE_STATE_STREAM_SIZE, __id, __type, __desc)

static bool
gfx_pipeline_variant_key_equal(const void *a, const void *b)
{
   return !memcmp(a, b, sizeof(struct dzn_graphics_pipeline_variant_key));
}

static uint32_t
gfx_pipeline_variant_key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct dzn_graphics_pipeline_variant_key));
}

struct dzn_cached_blob {
   struct vk_pipeline_cache_object base;
   uint8_t hash[SHA1_DIGEST_LENGTH];
   const void *data;
   size_t size;
};

static bool
dzn_cached_blob_serialize(struct vk_pipeline_cache_object *object,
                          struct blob *blob)
{
   struct dzn_cached_blob *cached_blob =
      container_of(object, struct dzn_cached_blob, base);

   blob_write_bytes(blob, cached_blob->data, cached_blob->size);
   return true;
}

static void
dzn_cached_blob_destroy(struct vk_pipeline_cache_object *object)
{
   struct dzn_cached_blob *shader =
      container_of(object, struct dzn_cached_blob, base);

   vk_free(&shader->base.device->alloc, shader);
}

static struct vk_pipeline_cache_object *
dzn_cached_blob_create(struct vk_device *device,
                       const void *hash,
                       const void *data,
                       size_t data_size);

static struct vk_pipeline_cache_object *
dzn_cached_blob_deserialize(struct vk_device *device,
                                   const void *key_data,
                                   size_t key_size,
                                   struct blob_reader *blob)
{
   size_t data_size = blob->end - blob->current;
   assert(key_size == SHA1_DIGEST_LENGTH);

   return dzn_cached_blob_create(device, key_data,
                                 blob_read_bytes(blob, data_size),
                                 data_size);
}

const struct vk_pipeline_cache_object_ops dzn_cached_blob_ops = {
   .serialize = dzn_cached_blob_serialize,
   .deserialize = dzn_cached_blob_deserialize,
   .destroy = dzn_cached_blob_destroy,
};


static struct vk_pipeline_cache_object *
dzn_cached_blob_create(struct vk_device *device,
                       const void *hash,
                       const void *data,
                       size_t data_size)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct dzn_cached_blob, blob, 1);
   VK_MULTIALLOC_DECL(&ma, uint8_t, copy, data_size);

   if (!vk_multialloc_alloc(&ma, &device->alloc,
                            VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   memcpy(blob->hash, hash, sizeof(blob->hash));

   vk_pipeline_cache_object_init(device, &blob->base,
                                 &dzn_cached_blob_ops,
                                 blob->hash, sizeof(blob->hash));

   if (data)
      memcpy(copy, data, data_size);
   blob->data = copy;
   blob->size = data_size;

   return &blob->base;
}

static VkResult
dzn_graphics_pipeline_prepare_for_variants(struct dzn_device *device,
                                           struct dzn_graphics_pipeline *pipeline)
{
   if (pipeline->variants)
      return VK_SUCCESS;

   pipeline->variants =
      _mesa_hash_table_create(NULL,
                              gfx_pipeline_variant_key_hash,
                              gfx_pipeline_variant_key_equal);
   if (!pipeline->variants)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   return VK_SUCCESS;
}

static dxil_spirv_shader_stage
to_dxil_shader_stage(VkShaderStageFlagBits in)
{
   switch (in) {
   case VK_SHADER_STAGE_VERTEX_BIT: return DXIL_SPIRV_SHADER_VERTEX;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return DXIL_SPIRV_SHADER_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return DXIL_SPIRV_SHADER_TESS_EVAL;
   case VK_SHADER_STAGE_GEOMETRY_BIT: return DXIL_SPIRV_SHADER_GEOMETRY;
   case VK_SHADER_STAGE_FRAGMENT_BIT: return DXIL_SPIRV_SHADER_FRAGMENT;
   case VK_SHADER_STAGE_COMPUTE_BIT: return DXIL_SPIRV_SHADER_COMPUTE;
   default: unreachable("Unsupported stage");
   }
}

static VkResult
dzn_pipeline_get_nir_shader(struct dzn_device *device,
                            const struct dzn_pipeline_layout *layout,
                            struct vk_pipeline_cache *cache,
                            const uint8_t *hash,
                            const VkPipelineShaderStageCreateInfo *stage_info,
                            gl_shader_stage stage,
                            enum dxil_spirv_yz_flip_mode yz_flip_mode,
                            uint16_t y_flip_mask, uint16_t z_flip_mask,
                            bool force_sample_rate_shading,
                            enum pipe_format *vi_conversions,
                            const nir_shader_compiler_options *nir_opts,
                            nir_shader **nir)
{
   if (cache) {
      *nir = vk_pipeline_cache_lookup_nir(cache, hash, SHA1_DIGEST_LENGTH,
                                          nir_opts, NULL, NULL);
       if (*nir)
          return VK_SUCCESS;
   }

   VK_FROM_HANDLE(vk_shader_module, module, stage_info->module);
   struct spirv_to_nir_options spirv_opts = {
      .caps = {
         .draw_parameters = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .shared_addr_format = nir_address_format_32bit_offset_as_64bit,

      /* use_deref_buffer_array_length + nir_lower_explicit_io force
       * get_ssbo_size to take in the return from load_vulkan_descriptor
       * instead of vulkan_resource_index. This makes it much easier to
       * get the DXIL handle for the SSBO.
       */
      .use_deref_buffer_array_length = true
   };

   VkResult result =
      vk_shader_module_to_nir(&device->vk, module, stage,
                              stage_info->pName, stage_info->pSpecializationInfo,
                              &spirv_opts, nir_opts, NULL, nir);
   if (result != VK_SUCCESS)
      return result;

   struct dxil_spirv_runtime_conf conf = {
      .runtime_data_cbv = {
         .register_space = DZN_REGISTER_SPACE_SYSVALS,
         .base_shader_register = 0,
      },
      .push_constant_cbv = {
         .register_space = DZN_REGISTER_SPACE_PUSH_CONSTANT,
         .base_shader_register = 0,
      },
      .zero_based_vertex_instance_id = false,
      .yz_flip = {
         .mode = yz_flip_mode,
         .y_mask = y_flip_mask,
         .z_mask = z_flip_mask,
      },
      .read_only_images_as_srvs = true,
      .force_sample_rate_shading = force_sample_rate_shading,
   };

   bool requires_runtime_data;
   dxil_spirv_nir_passes(*nir, &conf, &requires_runtime_data);

   if (stage == MESA_SHADER_VERTEX) {
      bool needs_conv = false;
      for (uint32_t i = 0; i < MAX_VERTEX_GENERIC_ATTRIBS; i++) {
         if (vi_conversions[i] != PIPE_FORMAT_NONE)
            needs_conv = true;
      }

      if (needs_conv)
         NIR_PASS_V(*nir, dxil_nir_lower_vs_vertex_conversion, vi_conversions);
   }

   if (cache)
      vk_pipeline_cache_add_nir(cache, hash, SHA1_DIGEST_LENGTH, *nir);

   return VK_SUCCESS;
}

static bool
adjust_resource_index_binding(struct nir_builder *builder, nir_instr *instr,
                              void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_vulkan_resource_index)
      return false;

   const struct dzn_pipeline_layout *layout = cb_data;
   unsigned set = nir_intrinsic_desc_set(intrin);
   unsigned binding = nir_intrinsic_binding(intrin);

   if (set >= layout->set_count ||
       binding >= layout->binding_translation[set].binding_count)
      return false;

   binding = layout->binding_translation[set].base_reg[binding];
   nir_intrinsic_set_binding(intrin, binding);

   return true;
}

static bool
adjust_var_bindings(nir_shader *shader,
                    const struct dzn_pipeline_layout *layout,
                    uint8_t *bindings_hash)
{
   uint32_t modes = nir_var_image | nir_var_uniform | nir_var_mem_ubo | nir_var_mem_ssbo;
   struct mesa_sha1 bindings_hash_ctx;

   if (bindings_hash)
      _mesa_sha1_init(&bindings_hash_ctx);

   nir_foreach_variable_with_modes(var, shader, modes) {
      if (var->data.mode == nir_var_uniform) {
         const struct glsl_type *type = glsl_without_array(var->type);

         if (!glsl_type_is_sampler(type) && !glsl_type_is_texture(type))
            continue;
      }

      unsigned s = var->data.descriptor_set, b = var->data.binding;

      if (s >= layout->set_count)
         continue;

      assert(b < layout->binding_translation[s].binding_count);
      var->data.binding = layout->binding_translation[s].base_reg[b];

      if (bindings_hash) {
         _mesa_sha1_update(&bindings_hash_ctx, &s, sizeof(s));
         _mesa_sha1_update(&bindings_hash_ctx, &b, sizeof(b));
         _mesa_sha1_update(&bindings_hash_ctx, &var->data.binding, sizeof(var->data.binding));
      }
   }

   if (bindings_hash)
      _mesa_sha1_final(&bindings_hash_ctx, bindings_hash);

   return nir_shader_instructions_pass(shader, adjust_resource_index_binding,
                                       nir_metadata_all, (void *)layout);
}

enum dxil_shader_model
   dzn_get_shader_model(const struct dzn_physical_device *pdev)
{
   static_assert(D3D_SHADER_MODEL_6_0 == 0x60 && SHADER_MODEL_6_0 == 0x60000, "Validating math below");
   static_assert(D3D_SHADER_MODEL_6_7 == 0x67 && SHADER_MODEL_6_7 == 0x60007, "Validating math below");
   return ((pdev->shader_model & 0xf0) << 12) | (pdev->shader_model & 0xf);
}

static VkResult
dzn_pipeline_compile_shader(struct dzn_device *device,
                            nir_shader *nir,
                            D3D12_SHADER_BYTECODE *slot)
{
   struct dzn_instance *instance =
      container_of(device->vk.physical->instance, struct dzn_instance, vk);
   struct dzn_physical_device *pdev =
      container_of(device->vk.physical, struct dzn_physical_device, vk);
   struct nir_to_dxil_options opts = {
      .environment = DXIL_ENVIRONMENT_VULKAN,
      .shader_model_max = dzn_get_shader_model(pdev),
#ifdef _WIN32
      .validator_version_max = dxil_get_validator_version(instance->dxil_validator),
#endif
   };
   struct blob dxil_blob;
   VkResult result = VK_SUCCESS;

   if (instance->debug_flags & DZN_DEBUG_NIR)
      nir_print_shader(nir, stderr);

   if (nir_to_dxil(nir, &opts, NULL, &dxil_blob)) {
      blob_finish_get_buffer(&dxil_blob, (void **)&slot->pShaderBytecode,
                             (size_t *)&slot->BytecodeLength);
   } else {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   if (dxil_blob.allocated)
      blob_finish(&dxil_blob);

   if (result != VK_SUCCESS)
      return result;

#ifdef _WIN32
   char *err;
   bool res = dxil_validate_module(instance->dxil_validator,
                                   (void *)slot->pShaderBytecode,
                                   slot->BytecodeLength, &err);

   if (instance->debug_flags & DZN_DEBUG_DXIL) {
      char *disasm = dxil_disasm_module(instance->dxil_validator,
                                        (void *)slot->pShaderBytecode,
                                        slot->BytecodeLength);
      if (disasm) {
         fprintf(stderr,
                 "== BEGIN SHADER ============================================\n"
                 "%s\n"
                 "== END SHADER ==============================================\n",
                  disasm);
         ralloc_free(disasm);
      }
   }

   if (!res) {
      if (err) {
         fprintf(stderr,
               "== VALIDATION ERROR =============================================\n"
               "%s\n"
               "== END ==========================================================\n",
               err);
         ralloc_free(err);
      }
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
#endif

   return VK_SUCCESS;
}

static D3D12_SHADER_BYTECODE *
dzn_pipeline_get_gfx_shader_slot(D3D12_PIPELINE_STATE_STREAM_DESC *stream,
                                 gl_shader_stage in)
{
   switch (in) {
   case MESA_SHADER_VERTEX: {
      d3d12_gfx_pipeline_state_stream_new_desc(stream, VS, D3D12_SHADER_BYTECODE, desc);
      return desc;
   }
   case MESA_SHADER_TESS_CTRL: {
      d3d12_gfx_pipeline_state_stream_new_desc(stream, DS, D3D12_SHADER_BYTECODE, desc);
      return desc;
   }
   case MESA_SHADER_TESS_EVAL: {
      d3d12_gfx_pipeline_state_stream_new_desc(stream, HS, D3D12_SHADER_BYTECODE, desc);
      return desc;
   }
   case MESA_SHADER_GEOMETRY: {
      d3d12_gfx_pipeline_state_stream_new_desc(stream, GS, D3D12_SHADER_BYTECODE, desc);
      return desc;
   }
   case MESA_SHADER_FRAGMENT: {
      d3d12_gfx_pipeline_state_stream_new_desc(stream, PS, D3D12_SHADER_BYTECODE, desc);
      return desc;
   }
   default: unreachable("Unsupported stage");
   }
}

struct dzn_cached_dxil_shader_header {
   gl_shader_stage stage;
   size_t size;
   uint8_t data[0];
};

static VkResult
dzn_pipeline_cache_lookup_dxil_shader(struct vk_pipeline_cache *cache,
                                      const uint8_t *dxil_hash,
                                      gl_shader_stage *stage,
                                      D3D12_SHADER_BYTECODE *bc)
{
   *stage = MESA_SHADER_NONE;

   if (!cache)
      return VK_SUCCESS;

   struct vk_pipeline_cache_object *cache_obj = NULL;

   cache_obj =
      vk_pipeline_cache_lookup_object(cache, dxil_hash, SHA1_DIGEST_LENGTH,
                                      &dzn_cached_blob_ops,
                                      NULL);
   if (!cache_obj)
      return VK_SUCCESS;

   struct dzn_cached_blob *cached_blob =
      container_of(cache_obj, struct dzn_cached_blob, base);
   VkResult ret = VK_SUCCESS;

   assert(sizeof(struct dzn_cached_dxil_shader_header) <= cached_blob->size);

   const struct dzn_cached_dxil_shader_header *info =
      (struct dzn_cached_dxil_shader_header *)(cached_blob->data);

   assert(sizeof(struct dzn_cached_dxil_shader_header) + info->size <= cached_blob->size);
   assert(info->stage > MESA_SHADER_NONE && info->stage < MESA_VULKAN_SHADER_STAGES);
   assert(info->size > 0);

   void *code = malloc(info->size);
   if (!code) {
      ret = vk_error(cache->base.device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto out;
   }

   memcpy(code, info->data, info->size);

   bc->pShaderBytecode = code;
   bc->BytecodeLength = info->size;
   *stage = info->stage;

out:
   vk_pipeline_cache_object_unref(cache_obj);
   return ret;
}

static void
dzn_pipeline_cache_add_dxil_shader(struct vk_pipeline_cache *cache,
                                   const uint8_t *dxil_hash,
                                   gl_shader_stage stage,
                                   const D3D12_SHADER_BYTECODE *bc)
{
   size_t size = sizeof(struct dzn_cached_dxil_shader_header) +
                 bc->BytecodeLength;

   struct vk_pipeline_cache_object *cache_obj =
      dzn_cached_blob_create(cache->base.device, dxil_hash, NULL, size);
   if (!cache_obj)
      return;

   struct dzn_cached_blob *cached_blob =
      container_of(cache_obj, struct dzn_cached_blob, base);
   struct dzn_cached_dxil_shader_header *info =
      (struct dzn_cached_dxil_shader_header *)(cached_blob->data);
   info->stage = stage;
   info->size = bc->BytecodeLength;
   memcpy(info->data, bc->pShaderBytecode, bc->BytecodeLength);

   cache_obj = vk_pipeline_cache_add_object(cache, cache_obj);
   vk_pipeline_cache_object_unref(cache_obj);
}

struct dzn_cached_gfx_pipeline_header {
   uint32_t stages;
   uint32_t input_count;
};

static VkResult
dzn_pipeline_cache_lookup_gfx_pipeline(struct dzn_graphics_pipeline *pipeline,
                                       struct vk_pipeline_cache *cache,
                                       const uint8_t *pipeline_hash,
                                       bool *cache_hit)
{
   *cache_hit = false;

   if (!cache)
      return VK_SUCCESS;

   struct vk_pipeline_cache_object *cache_obj = NULL;

   cache_obj =
      vk_pipeline_cache_lookup_object(cache, pipeline_hash, SHA1_DIGEST_LENGTH,
                                      &dzn_cached_blob_ops,
                                      NULL);
   if (!cache_obj)
      return VK_SUCCESS;

   struct dzn_cached_blob *cached_blob =
      container_of(cache_obj, struct dzn_cached_blob, base);
   D3D12_PIPELINE_STATE_STREAM_DESC *stream_desc =
      &pipeline->templates.stream_desc;

   const struct dzn_cached_gfx_pipeline_header *info =
      (const struct dzn_cached_gfx_pipeline_header *)(cached_blob->data);
   size_t offset = sizeof(*info);

   assert(cached_blob->size >= sizeof(*info));

   if (info->input_count > 0) {
      offset = ALIGN_POT(offset, alignof(D3D12_INPUT_LAYOUT_DESC));
      const D3D12_INPUT_ELEMENT_DESC *inputs =
         (const D3D12_INPUT_ELEMENT_DESC *)((uint8_t *)cached_blob->data + offset);

      assert(cached_blob->size >= offset + sizeof(*inputs) * info->input_count);

      memcpy(pipeline->templates.inputs, inputs,
             sizeof(*inputs) * info->input_count);
      d3d12_gfx_pipeline_state_stream_new_desc(stream_desc, INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC, desc);
      desc->pInputElementDescs = pipeline->templates.inputs;
      desc->NumElements = info->input_count;
      offset += sizeof(*inputs) * info->input_count;
   }

   assert(cached_blob->size == offset + util_bitcount(info->stages) * SHA1_DIGEST_LENGTH);

   u_foreach_bit(s, info->stages) {
      uint8_t *dxil_hash = (uint8_t *)cached_blob->data + offset;
      gl_shader_stage stage;

      D3D12_SHADER_BYTECODE *slot =
         dzn_pipeline_get_gfx_shader_slot(stream_desc, s);

      VkResult ret =
         dzn_pipeline_cache_lookup_dxil_shader(cache, dxil_hash, &stage, slot);
      if (ret != VK_SUCCESS)
         return ret;

      assert(stage == s);
      offset += SHA1_DIGEST_LENGTH;
   }

   *cache_hit = true;

   vk_pipeline_cache_object_unref(cache_obj);
   return VK_SUCCESS;
}

static void
dzn_pipeline_cache_add_gfx_pipeline(struct dzn_graphics_pipeline *pipeline,
                                    struct vk_pipeline_cache *cache,
                                    uint32_t vertex_input_count,
                                    const uint8_t *pipeline_hash,
                                    const uint8_t *const *dxil_hashes)
{
   size_t offset =
      ALIGN_POT(sizeof(struct dzn_cached_gfx_pipeline_header), alignof(D3D12_INPUT_ELEMENT_DESC)) +
      (sizeof(D3D12_INPUT_ELEMENT_DESC) * vertex_input_count);
   uint32_t stages = 0;

   for (uint32_t i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (pipeline->templates.shaders[i].bc) {
         stages |= BITFIELD_BIT(i);
         offset += SHA1_DIGEST_LENGTH;
      }
   }

   struct vk_pipeline_cache_object *cache_obj =
      dzn_cached_blob_create(cache->base.device, pipeline_hash, NULL, offset);
   if (!cache_obj)
      return;

   struct dzn_cached_blob *cached_blob =
      container_of(cache_obj, struct dzn_cached_blob, base);

   offset = 0;
   struct dzn_cached_gfx_pipeline_header *info =
      (struct dzn_cached_gfx_pipeline_header *)(cached_blob->data);

   info->input_count = vertex_input_count;
   info->stages = stages;

   offset = ALIGN_POT(offset + sizeof(*info), alignof(D3D12_INPUT_ELEMENT_DESC));

   D3D12_INPUT_ELEMENT_DESC *inputs =
      (D3D12_INPUT_ELEMENT_DESC *)((uint8_t *)cached_blob->data + offset);
   memcpy(inputs, pipeline->templates.inputs,
          sizeof(*inputs) * vertex_input_count);
   offset += sizeof(*inputs) * vertex_input_count;

   u_foreach_bit(s, stages) {
      uint8_t *dxil_hash = (uint8_t *)cached_blob->data + offset;

      memcpy(dxil_hash, dxil_hashes[s], SHA1_DIGEST_LENGTH);
      offset += SHA1_DIGEST_LENGTH;
   }

   cache_obj = vk_pipeline_cache_add_object(cache, cache_obj);
   vk_pipeline_cache_object_unref(cache_obj);
}

static void
dzn_graphics_pipeline_hash_attribs(D3D12_INPUT_ELEMENT_DESC *attribs,
                                   enum pipe_format *vi_conversions,
                                   uint8_t *result)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);
   _mesa_sha1_update(&ctx, attribs, sizeof(*attribs) * MAX_VERTEX_GENERIC_ATTRIBS);
   _mesa_sha1_update(&ctx, vi_conversions, sizeof(*vi_conversions) * MAX_VERTEX_GENERIC_ATTRIBS);
   _mesa_sha1_final(&ctx, result);
}

static VkResult
dzn_graphics_pipeline_compile_shaders(struct dzn_device *device,
                                      struct dzn_graphics_pipeline *pipeline,
                                      struct vk_pipeline_cache *cache,
                                      const struct dzn_pipeline_layout *layout,
                                      D3D12_PIPELINE_STATE_STREAM_DESC *out,
                                      D3D12_INPUT_ELEMENT_DESC *attribs,
                                      enum pipe_format *vi_conversions,
                                      const VkGraphicsPipelineCreateInfo *info)
{
   const VkPipelineViewportStateCreateInfo *vp_info =
      info->pRasterizationState->rasterizerDiscardEnable ?
      NULL : info->pViewportState;
   struct {
      const VkPipelineShaderStageCreateInfo *info;
      uint8_t spirv_hash[SHA1_DIGEST_LENGTH];
      uint8_t dxil_hash[SHA1_DIGEST_LENGTH];
   } stages[MESA_VULKAN_SHADER_STAGES] = { 0 };
   const uint8_t *dxil_hashes[MESA_VULKAN_SHADER_STAGES] = { 0 };
   uint8_t attribs_hash[SHA1_DIGEST_LENGTH];
   uint8_t pipeline_hash[SHA1_DIGEST_LENGTH];
   gl_shader_stage yz_flip_stage = MESA_SHADER_NONE;
   uint32_t active_stage_mask = 0;
   VkResult ret;

   /* First step: collect stage info in a table indexed by gl_shader_stage
    * so we can iterate over stages in pipeline order or reverse pipeline
    * order.
    */
   for (uint32_t i = 0; i < info->stageCount; i++) {
      gl_shader_stage stage =
         vk_to_mesa_shader_stage(info->pStages[i].stage);

      assert(stage <= MESA_SHADER_FRAGMENT);

      if ((stage == MESA_SHADER_VERTEX ||
           stage == MESA_SHADER_TESS_EVAL ||
           stage == MESA_SHADER_GEOMETRY) &&
          yz_flip_stage < stage)
         yz_flip_stage = stage;

      if (stage == MESA_SHADER_FRAGMENT &&
          info->pRasterizationState &&
          (info->pRasterizationState->rasterizerDiscardEnable ||
           info->pRasterizationState->cullMode == VK_CULL_MODE_FRONT_AND_BACK)) {
         /* Disable rasterization (AKA leave fragment shader NULL) when
          * front+back culling or discard is set.
          */
         continue;
      }

      stages[stage].info = &info->pStages[i];
      active_stage_mask |= BITFIELD_BIT(stage);
   }

   enum dxil_spirv_yz_flip_mode yz_flip_mode = DXIL_SPIRV_YZ_FLIP_NONE;
   uint16_t y_flip_mask = 0, z_flip_mask = 0;

   if (pipeline->vp.dynamic) {
      yz_flip_mode = DXIL_SPIRV_YZ_FLIP_CONDITIONAL;
   } else if (vp_info) {
      for (uint32_t i = 0; vp_info->pViewports && i < vp_info->viewportCount; i++) {
         if (vp_info->pViewports[i].height > 0)
            y_flip_mask |= BITFIELD_BIT(i);

         if (vp_info->pViewports[i].minDepth > vp_info->pViewports[i].maxDepth)
            z_flip_mask |= BITFIELD_BIT(i);
      }

      if (y_flip_mask && z_flip_mask)
         yz_flip_mode = DXIL_SPIRV_YZ_FLIP_UNCONDITIONAL;
      else if (z_flip_mask)
         yz_flip_mode = DXIL_SPIRV_Z_FLIP_UNCONDITIONAL;
      else if (y_flip_mask)
         yz_flip_mode = DXIL_SPIRV_Y_FLIP_UNCONDITIONAL;
   }

   bool force_sample_rate_shading =
      info->pMultisampleState &&
      info->pMultisampleState->sampleShadingEnable;

   if (cache) {
      dzn_graphics_pipeline_hash_attribs(attribs, vi_conversions, attribs_hash);

      struct mesa_sha1 pipeline_hash_ctx;

      _mesa_sha1_init(&pipeline_hash_ctx);
      _mesa_sha1_update(&pipeline_hash_ctx, attribs_hash, sizeof(attribs_hash));
      _mesa_sha1_update(&pipeline_hash_ctx, &yz_flip_mode, sizeof(yz_flip_mode));
      _mesa_sha1_update(&pipeline_hash_ctx, &y_flip_mask, sizeof(y_flip_mask));
      _mesa_sha1_update(&pipeline_hash_ctx, &z_flip_mask, sizeof(z_flip_mask));
      _mesa_sha1_update(&pipeline_hash_ctx, &force_sample_rate_shading, sizeof(force_sample_rate_shading));

      u_foreach_bit(stage, active_stage_mask) {
         vk_pipeline_hash_shader_stage(stages[stage].info, stages[stage].spirv_hash);
         _mesa_sha1_update(&pipeline_hash_ctx, stages[stage].spirv_hash, sizeof(stages[stage].spirv_hash));
         _mesa_sha1_update(&pipeline_hash_ctx, layout->stages[stage].hash, sizeof(layout->stages[stage].hash));
      }
      _mesa_sha1_final(&pipeline_hash_ctx, pipeline_hash);

      bool cache_hit;
      ret = dzn_pipeline_cache_lookup_gfx_pipeline(pipeline, cache, pipeline_hash,
                                                   &cache_hit);
      if (ret != VK_SUCCESS)
         return ret;

      if (cache_hit)
         return VK_SUCCESS;
   }

   /* Second step: get NIR shaders for all stages. */
   nir_shader_compiler_options nir_opts = *dxil_get_nir_compiler_options();
   nir_opts.lower_base_vertex = true;
   u_foreach_bit(stage, active_stage_mask) {
      struct mesa_sha1 nir_hash_ctx;
      uint8_t nir_hash[SHA1_DIGEST_LENGTH];

      if (cache) {
         _mesa_sha1_init(&nir_hash_ctx);
         if (stage == MESA_SHADER_VERTEX)
            _mesa_sha1_update(&nir_hash_ctx, attribs_hash, sizeof(attribs_hash));
         if (stage == yz_flip_stage) {
            _mesa_sha1_update(&nir_hash_ctx, &yz_flip_mode, sizeof(yz_flip_mode));
            _mesa_sha1_update(&nir_hash_ctx, &y_flip_mask, sizeof(y_flip_mask));
            _mesa_sha1_update(&nir_hash_ctx, &z_flip_mask, sizeof(z_flip_mask));
         }
         _mesa_sha1_update(&nir_hash_ctx, stages[stage].spirv_hash, sizeof(stages[stage].spirv_hash));
         _mesa_sha1_final(&nir_hash_ctx, nir_hash);
      }

      ret = dzn_pipeline_get_nir_shader(device, layout,
                                        cache, nir_hash,
                                        stages[stage].info, stage,
                                        stage == yz_flip_stage ? yz_flip_mode : DXIL_SPIRV_YZ_FLIP_NONE,
                                        y_flip_mask, z_flip_mask,
                                        stage == MESA_SHADER_FRAGMENT ? force_sample_rate_shading : false,
                                        vi_conversions,
                                        &nir_opts, &pipeline->templates.shaders[stage].nir);
      if (ret != VK_SUCCESS)
         return ret;
   }

   /* Third step: link those NIR shaders. We iterate in reverse order
    * so we can eliminate outputs that are never read by the next stage.
    */
   uint32_t link_mask = active_stage_mask;
   while (link_mask != 0) {
      gl_shader_stage stage = util_last_bit(link_mask) - 1;
      link_mask &= ~BITFIELD_BIT(stage);
      gl_shader_stage prev_stage = util_last_bit(link_mask) - 1;

      assert(pipeline->templates.shaders[stage].nir);
      dxil_spirv_nir_link(pipeline->templates.shaders[stage].nir,
                          prev_stage != MESA_SHADER_NONE ?
                          pipeline->templates.shaders[prev_stage].nir : NULL);
   }

   u_foreach_bit(stage, active_stage_mask) {
      uint8_t bindings_hash[SHA1_DIGEST_LENGTH];

      NIR_PASS_V(pipeline->templates.shaders[stage].nir, adjust_var_bindings, layout,
                 cache ? bindings_hash : NULL);

      if (cache) {
         struct mesa_sha1 dxil_hash_ctx;

         _mesa_sha1_init(&dxil_hash_ctx);

         if (stage == MESA_SHADER_VERTEX)
            _mesa_sha1_update(&dxil_hash_ctx, attribs_hash, sizeof(attribs_hash));

         if (stage == yz_flip_stage) {
            _mesa_sha1_update(&dxil_hash_ctx, &yz_flip_mode, sizeof(yz_flip_mode));
            _mesa_sha1_update(&dxil_hash_ctx, &y_flip_mask, sizeof(y_flip_mask));
            _mesa_sha1_update(&dxil_hash_ctx, &z_flip_mask, sizeof(z_flip_mask));
         }

         if (stage == MESA_SHADER_FRAGMENT)
            _mesa_sha1_update(&dxil_hash_ctx, &force_sample_rate_shading, sizeof(force_sample_rate_shading));

         _mesa_sha1_update(&dxil_hash_ctx, stages[stage].spirv_hash, sizeof(stages[stage].spirv_hash));
         _mesa_sha1_update(&dxil_hash_ctx, bindings_hash, sizeof(bindings_hash));
         _mesa_sha1_final(&dxil_hash_ctx, stages[stage].dxil_hash);
         dxil_hashes[stage] = stages[stage].dxil_hash;

         gl_shader_stage cached_stage;
         D3D12_SHADER_BYTECODE bc;
         ret = dzn_pipeline_cache_lookup_dxil_shader(cache, stages[stage].dxil_hash, &cached_stage, &bc);
         if (ret != VK_SUCCESS)
            return ret;

         if (cached_stage != MESA_SHADER_NONE) {
            assert(cached_stage == stage);
            D3D12_SHADER_BYTECODE *slot =
               dzn_pipeline_get_gfx_shader_slot(out, stage);
            *slot = bc;
            pipeline->templates.shaders[stage].bc = slot;
         }
      }
   }

   uint32_t vert_input_count = 0;
   if (pipeline->templates.shaders[MESA_SHADER_VERTEX].nir) {
      /* Now, declare one D3D12_INPUT_ELEMENT_DESC per VS input variable, so
       * we can handle location overlaps properly.
       */
      nir_foreach_shader_in_variable(var, pipeline->templates.shaders[MESA_SHADER_VERTEX].nir) {
         assert(var->data.location >= VERT_ATTRIB_GENERIC0);
         unsigned loc = var->data.location - VERT_ATTRIB_GENERIC0;
         assert(vert_input_count < D3D12_VS_INPUT_REGISTER_COUNT);
         assert(loc < MAX_VERTEX_GENERIC_ATTRIBS);

         pipeline->templates.inputs[vert_input_count] = attribs[loc];
         pipeline->templates.inputs[vert_input_count].SemanticIndex = vert_input_count;
         var->data.driver_location = vert_input_count++;
      }

      if (vert_input_count > 0) {
         d3d12_gfx_pipeline_state_stream_new_desc(out, INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC, desc);
         desc->pInputElementDescs = pipeline->templates.inputs;
         desc->NumElements = vert_input_count;
      }
   }

   /* Last step: translate NIR shaders into DXIL modules */
   u_foreach_bit(stage, active_stage_mask) {
      /* Cache hit, we can skip the compilation. */
      if (pipeline->templates.shaders[stage].bc)
         continue;

      if (stage == MESA_SHADER_FRAGMENT) {
         gl_shader_stage prev_stage =
            util_last_bit(active_stage_mask & BITFIELD_MASK(MESA_SHADER_FRAGMENT)) - 1;
         /* Disable rasterization if the last geometry stage doesn't
          * write the position.
          */
         if (prev_stage == MESA_SHADER_NONE ||
             !(pipeline->templates.shaders[prev_stage].nir->info.outputs_written & VARYING_BIT_POS))
            continue;
      }

      D3D12_SHADER_BYTECODE *slot =
         dzn_pipeline_get_gfx_shader_slot(out, stage);

      ret = dzn_pipeline_compile_shader(device, pipeline->templates.shaders[stage].nir, slot);
      if (ret != VK_SUCCESS)
         return ret;

      pipeline->templates.shaders[stage].bc = slot;

      if (cache)
         dzn_pipeline_cache_add_dxil_shader(cache, stages[stage].dxil_hash, stage, slot);
   }

   if (cache)
      dzn_pipeline_cache_add_gfx_pipeline(pipeline, cache, vert_input_count, pipeline_hash,
                                          dxil_hashes);

   return VK_SUCCESS;
}

VkFormat
dzn_graphics_pipeline_patch_vi_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
   case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
   case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
   case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
   case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
   case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
   case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
      return VK_FORMAT_R32_UINT;
   case VK_FORMAT_R8G8B8A8_SSCALED:
      return VK_FORMAT_R8G8B8A8_SINT;
   case VK_FORMAT_R8G8B8A8_USCALED:
      return VK_FORMAT_R8G8B8A8_UINT;
   case VK_FORMAT_R16G16B16A16_USCALED:
      return VK_FORMAT_R16G16B16A16_UINT;
   case VK_FORMAT_R16G16B16A16_SSCALED:
      return VK_FORMAT_R16G16B16A16_SINT;
   default:
      return format;
   }
}

static VkResult
dzn_graphics_pipeline_translate_vi(struct dzn_graphics_pipeline *pipeline,
                                   const VkGraphicsPipelineCreateInfo *in,
                                   D3D12_INPUT_ELEMENT_DESC *inputs,
                                   enum pipe_format *vi_conversions)
{
   const VkPipelineVertexInputStateCreateInfo *in_vi =
      in->pVertexInputState;
   const VkPipelineVertexInputDivisorStateCreateInfoEXT *divisors =
      (const VkPipelineVertexInputDivisorStateCreateInfoEXT *)
      vk_find_struct_const(in_vi, PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT);

   if (!in_vi->vertexAttributeDescriptionCount)
      return VK_SUCCESS;

   D3D12_INPUT_CLASSIFICATION slot_class[MAX_VBS];

   pipeline->vb.count = 0;
   for (uint32_t i = 0; i < in_vi->vertexBindingDescriptionCount; i++) {
      const struct VkVertexInputBindingDescription *bdesc =
         &in_vi->pVertexBindingDescriptions[i];

      pipeline->vb.count = MAX2(pipeline->vb.count, bdesc->binding + 1);
      pipeline->vb.strides[bdesc->binding] = bdesc->stride;
      if (bdesc->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE) {
         slot_class[bdesc->binding] = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
      } else {
         assert(bdesc->inputRate == VK_VERTEX_INPUT_RATE_VERTEX);
         slot_class[bdesc->binding] = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      }
   }

   for (uint32_t i = 0; i < in_vi->vertexAttributeDescriptionCount; i++) {
      const VkVertexInputAttributeDescription *attr =
         &in_vi->pVertexAttributeDescriptions[i];
      const VkVertexInputBindingDivisorDescriptionEXT *divisor = NULL;

      if (slot_class[attr->binding] == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA &&
          divisors) {
         for (uint32_t d = 0; d < divisors->vertexBindingDivisorCount; d++) {
            if (attr->binding == divisors->pVertexBindingDivisors[d].binding) {
               divisor = &divisors->pVertexBindingDivisors[d];
               break;
            }
         }
      }

      VkFormat patched_format = dzn_graphics_pipeline_patch_vi_format(attr->format);
      if (patched_format != attr->format)
         vi_conversions[attr->location] = vk_format_to_pipe_format(attr->format);

      /* nir_to_dxil() name all vertex inputs as TEXCOORDx */
      inputs[attr->location] = (D3D12_INPUT_ELEMENT_DESC) {
         .SemanticName = "TEXCOORD",
         .Format = dzn_buffer_get_dxgi_format(patched_format),
         .InputSlot = attr->binding,
         .InputSlotClass = slot_class[attr->binding],
         .InstanceDataStepRate =
            divisor ? divisor->divisor :
            slot_class[attr->binding] == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ? 1 : 0,
         .AlignedByteOffset = attr->offset,
      };
   }

   return VK_SUCCESS;
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE
to_prim_topology_type(VkPrimitiveTopology in)
{
   switch (in) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
   default: unreachable("Invalid primitive topology");
   }
}

static D3D12_PRIMITIVE_TOPOLOGY
to_prim_topology(VkPrimitiveTopology in, unsigned patch_control_points)
{
   switch (in) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
   /* Triangle fans are emulated using an intermediate index buffer. */
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      assert(patch_control_points);
      return (D3D12_PRIMITIVE_TOPOLOGY)(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + patch_control_points - 1);
   default: unreachable("Invalid primitive topology");
   }
}

static VkResult
dzn_graphics_pipeline_translate_ia(struct dzn_device *device,
                                   struct dzn_graphics_pipeline *pipeline,
                                   D3D12_PIPELINE_STATE_STREAM_DESC *out,
                                   const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineInputAssemblyStateCreateInfo *in_ia =
      in->pInputAssemblyState;
   bool has_tes = false;
   for (uint32_t i = 0; i < in->stageCount; i++) {
      if (in->pStages[i].stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
          in->pStages[i].stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
         has_tes = true;
         break;
      }
   }
   const VkPipelineTessellationStateCreateInfo *in_tes =
      has_tes ? in->pTessellationState : NULL;
   VkResult ret = VK_SUCCESS;

   d3d12_gfx_pipeline_state_stream_new_desc(out, PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE, prim_top_type);
   *prim_top_type = to_prim_topology_type(in_ia->topology);
   pipeline->ia.triangle_fan = in_ia->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
   pipeline->ia.topology =
      to_prim_topology(in_ia->topology, in_tes ? in_tes->patchControlPoints : 0);

   if (in_ia->primitiveRestartEnable) {
      d3d12_gfx_pipeline_state_stream_new_desc(out, IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE, ib_strip_cut);
      pipeline->templates.desc_offsets.ib_strip_cut =
         (uintptr_t)ib_strip_cut - (uintptr_t)out->pPipelineStateSubobjectStream;
      *ib_strip_cut = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
      ret = dzn_graphics_pipeline_prepare_for_variants(device, pipeline);
   }

   return ret;
}

static D3D12_FILL_MODE
translate_polygon_mode(VkPolygonMode in)
{
   switch (in) {
   case VK_POLYGON_MODE_FILL: return D3D12_FILL_MODE_SOLID;
   case VK_POLYGON_MODE_LINE: return D3D12_FILL_MODE_WIREFRAME;
   default: unreachable("Unsupported polygon mode");
   }
}

static D3D12_CULL_MODE
translate_cull_mode(VkCullModeFlags in)
{
   switch (in) {
   case VK_CULL_MODE_NONE: return D3D12_CULL_MODE_NONE;
   case VK_CULL_MODE_FRONT_BIT: return D3D12_CULL_MODE_FRONT;
   case VK_CULL_MODE_BACK_BIT: return D3D12_CULL_MODE_BACK;
   /* Front+back face culling is equivalent to 'rasterization disabled' */
   case VK_CULL_MODE_FRONT_AND_BACK: return D3D12_CULL_MODE_NONE;
   default: unreachable("Unsupported cull mode");
   }
}

static int32_t
translate_depth_bias(double depth_bias)
{
   if (depth_bias > INT32_MAX)
      return INT32_MAX;
   else if (depth_bias < INT32_MIN)
      return INT32_MIN;

   return depth_bias;
}

static void
dzn_graphics_pipeline_translate_rast(struct dzn_graphics_pipeline *pipeline,
                                     D3D12_PIPELINE_STATE_STREAM_DESC *out,
                                     const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineViewportStateCreateInfo *in_vp =
      in_rast->rasterizerDiscardEnable ? NULL : in->pViewportState;

   if (in_vp) {
      pipeline->vp.count = in_vp->viewportCount;
      if (in_vp->pViewports) {
         for (uint32_t i = 0; in_vp->pViewports && i < in_vp->viewportCount; i++)
            dzn_translate_viewport(&pipeline->vp.desc[i], &in_vp->pViewports[i]);
      }

      pipeline->scissor.count = in_vp->scissorCount;
      if (in_vp->pScissors) {
         for (uint32_t i = 0; i < in_vp->scissorCount; i++)
            dzn_translate_rect(&pipeline->scissor.desc[i], &in_vp->pScissors[i]);
      }
   }

   d3d12_gfx_pipeline_state_stream_new_desc(out, RASTERIZER, D3D12_RASTERIZER_DESC, desc);
   pipeline->templates.desc_offsets.rast =
      (uintptr_t)desc - (uintptr_t)out->pPipelineStateSubobjectStream;
   desc->DepthClipEnable = !in_rast->depthClampEnable;
   desc->FillMode = translate_polygon_mode(in_rast->polygonMode);
   desc->CullMode = translate_cull_mode(in_rast->cullMode);
   desc->FrontCounterClockwise =
      in_rast->frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE;
   if (in_rast->depthBiasEnable) {
      desc->DepthBias = translate_depth_bias(in_rast->depthBiasConstantFactor);
      desc->SlopeScaledDepthBias = in_rast->depthBiasSlopeFactor;
      desc->DepthBiasClamp = in_rast->depthBiasClamp;
   }

   assert(in_rast->lineWidth == 1.0f);
}

static void
dzn_graphics_pipeline_translate_ms(struct dzn_graphics_pipeline *pipeline,
                                   D3D12_PIPELINE_STATE_STREAM_DESC *out,
                                   const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineMultisampleStateCreateInfo *in_ms =
      in_rast->rasterizerDiscardEnable ? NULL : in->pMultisampleState;

   if (!in_ms)
      return;

   /* TODO: minSampleShading (use VRS), alphaToOneEnable */
   d3d12_gfx_pipeline_state_stream_new_desc(out, SAMPLE_DESC, DXGI_SAMPLE_DESC, desc);
   desc->Count = in_ms ? in_ms->rasterizationSamples : 1;
   desc->Quality = 0;

   if (!in_ms->pSampleMask)
      return;

   d3d12_gfx_pipeline_state_stream_new_desc(out, SAMPLE_MASK, UINT, mask);
   *mask = *in_ms->pSampleMask;
}

static D3D12_STENCIL_OP
translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP: return D3D12_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO: return D3D12_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE: return D3D12_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return D3D12_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return D3D12_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP: return D3D12_STENCIL_OP_INCR;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP: return D3D12_STENCIL_OP_DECR;
   case VK_STENCIL_OP_INVERT: return D3D12_STENCIL_OP_INVERT;
   default: unreachable("Invalid stencil op");
   }
}

static void
translate_stencil_test(struct dzn_graphics_pipeline *pipeline,
                       D3D12_DEPTH_STENCIL_DESC1 *out,
                       const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineDepthStencilStateCreateInfo *in_zsa =
      in->pDepthStencilState;

   bool front_test_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_FRONT_BIT) &&
      in_zsa->front.compareOp != VK_COMPARE_OP_NEVER &&
      in_zsa->front.compareOp != VK_COMPARE_OP_ALWAYS &&
      (pipeline->zsa.stencil_test.dynamic_compare_mask ||
       in_zsa->front.compareMask != 0);
   bool back_test_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_BACK_BIT) &&
      in_zsa->back.compareOp != VK_COMPARE_OP_NEVER &&
      in_zsa->back.compareOp != VK_COMPARE_OP_ALWAYS &&
      (pipeline->zsa.stencil_test.dynamic_compare_mask ||
       in_zsa->back.compareMask != 0);

   if (front_test_uses_ref && pipeline->zsa.stencil_test.dynamic_compare_mask)
      pipeline->zsa.stencil_test.front.compare_mask = UINT32_MAX;
   else if (front_test_uses_ref)
      pipeline->zsa.stencil_test.front.compare_mask = in_zsa->front.compareMask;
   else
      pipeline->zsa.stencil_test.front.compare_mask = 0;

   if (back_test_uses_ref && pipeline->zsa.stencil_test.dynamic_compare_mask)
      pipeline->zsa.stencil_test.back.compare_mask = UINT32_MAX;
   else if (back_test_uses_ref)
      pipeline->zsa.stencil_test.back.compare_mask = in_zsa->back.compareMask;
   else
      pipeline->zsa.stencil_test.back.compare_mask = 0;

   bool diff_wr_mask =
      in->pRasterizationState->cullMode == VK_CULL_MODE_NONE &&
      (pipeline->zsa.stencil_test.dynamic_write_mask ||
       in_zsa->back.writeMask != in_zsa->front.writeMask);
   bool diff_ref =
      in->pRasterizationState->cullMode == VK_CULL_MODE_NONE &&
      (pipeline->zsa.stencil_test.dynamic_ref ||
       in_zsa->back.reference != in_zsa->front.reference);
   bool diff_cmp_mask =
      back_test_uses_ref && front_test_uses_ref &&
      (pipeline->zsa.stencil_test.dynamic_compare_mask ||
       pipeline->zsa.stencil_test.front.compare_mask != pipeline->zsa.stencil_test.back.compare_mask);

   if (diff_cmp_mask || diff_wr_mask)
      pipeline->zsa.stencil_test.independent_front_back = true;

   bool back_wr_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_BACK_BIT) &&
      ((in_zsa->back.compareOp != VK_COMPARE_OP_ALWAYS &&
        in_zsa->back.failOp == VK_STENCIL_OP_REPLACE) ||
       (in_zsa->back.compareOp != VK_COMPARE_OP_NEVER &&
        (!in_zsa->depthTestEnable || in_zsa->depthCompareOp != VK_COMPARE_OP_NEVER) &&
        in_zsa->back.passOp == VK_STENCIL_OP_REPLACE) ||
       (in_zsa->depthTestEnable &&
        in_zsa->depthCompareOp != VK_COMPARE_OP_ALWAYS &&
        in_zsa->back.depthFailOp == VK_STENCIL_OP_REPLACE));
   bool front_wr_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_FRONT_BIT) &&
      ((in_zsa->front.compareOp != VK_COMPARE_OP_ALWAYS &&
        in_zsa->front.failOp == VK_STENCIL_OP_REPLACE) ||
       (in_zsa->front.compareOp != VK_COMPARE_OP_NEVER &&
        (!in_zsa->depthTestEnable || in_zsa->depthCompareOp != VK_COMPARE_OP_NEVER) &&
        in_zsa->front.passOp == VK_STENCIL_OP_REPLACE) ||
       (in_zsa->depthTestEnable &&
        in_zsa->depthCompareOp != VK_COMPARE_OP_ALWAYS &&
        in_zsa->front.depthFailOp == VK_STENCIL_OP_REPLACE));

   pipeline->zsa.stencil_test.front.write_mask =
      (pipeline->zsa.stencil_test.dynamic_write_mask ||
       (in->pRasterizationState->cullMode & VK_CULL_MODE_FRONT_BIT)) ?
      0 : in_zsa->front.writeMask;
   pipeline->zsa.stencil_test.back.write_mask =
      (pipeline->zsa.stencil_test.dynamic_write_mask ||
       (in->pRasterizationState->cullMode & VK_CULL_MODE_BACK_BIT)) ?
      0 : in_zsa->back.writeMask;

   pipeline->zsa.stencil_test.front.uses_ref = front_test_uses_ref || front_wr_uses_ref;
   pipeline->zsa.stencil_test.back.uses_ref = back_test_uses_ref || back_wr_uses_ref;

   if (diff_ref &&
       pipeline->zsa.stencil_test.front.uses_ref &&
       pipeline->zsa.stencil_test.back.uses_ref)
      pipeline->zsa.stencil_test.independent_front_back = true;

   pipeline->zsa.stencil_test.front.ref =
      pipeline->zsa.stencil_test.dynamic_ref ? 0 : in_zsa->front.reference;
   pipeline->zsa.stencil_test.back.ref =
      pipeline->zsa.stencil_test.dynamic_ref ? 0 : in_zsa->back.reference;

   /* FIXME: We don't support independent {compare,write}_mask and stencil
    * reference. Until we have proper support for independent front/back
    * stencil test, let's prioritize the front setup when both are active.
    */
   out->StencilReadMask =
      front_test_uses_ref ?
      pipeline->zsa.stencil_test.front.compare_mask :
      back_test_uses_ref ?
      pipeline->zsa.stencil_test.back.compare_mask : 0;
   out->StencilWriteMask =
      pipeline->zsa.stencil_test.front.write_mask ?
      pipeline->zsa.stencil_test.front.write_mask :
      pipeline->zsa.stencil_test.back.write_mask;

   assert(!pipeline->zsa.stencil_test.independent_front_back);
}

static void
dzn_graphics_pipeline_translate_zsa(struct dzn_graphics_pipeline *pipeline,
                                    D3D12_PIPELINE_STATE_STREAM_DESC *out,
                                    const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineDepthStencilStateCreateInfo *in_zsa =
      in_rast->rasterizerDiscardEnable ? NULL : in->pDepthStencilState;

   if (!in_zsa)
      return;

   d3d12_gfx_pipeline_state_stream_new_desc(out, DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1, desc);
   pipeline->templates.desc_offsets.ds =
      (uintptr_t)desc - (uintptr_t)out->pPipelineStateSubobjectStream;

   desc->DepthEnable =
      in_zsa->depthTestEnable || in_zsa->depthBoundsTestEnable;
   desc->DepthWriteMask =
      in_zsa->depthWriteEnable ?
      D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
   desc->DepthFunc =
      in_zsa->depthTestEnable ?
      dzn_translate_compare_op(in_zsa->depthCompareOp) :
      D3D12_COMPARISON_FUNC_ALWAYS;
   pipeline->zsa.depth_bounds.enable = in_zsa->depthBoundsTestEnable;
   pipeline->zsa.depth_bounds.min = in_zsa->minDepthBounds;
   pipeline->zsa.depth_bounds.max = in_zsa->maxDepthBounds;
   desc->DepthBoundsTestEnable = in_zsa->depthBoundsTestEnable;
   desc->StencilEnable = in_zsa->stencilTestEnable;
   if (in_zsa->stencilTestEnable) {
      desc->FrontFace.StencilFailOp =
        translate_stencil_op(in_zsa->front.failOp);
      desc->FrontFace.StencilDepthFailOp =
        translate_stencil_op(in_zsa->front.depthFailOp);
      desc->FrontFace.StencilPassOp =
        translate_stencil_op(in_zsa->front.passOp);
      desc->FrontFace.StencilFunc =
        dzn_translate_compare_op(in_zsa->front.compareOp);
      desc->BackFace.StencilFailOp =
        translate_stencil_op(in_zsa->back.failOp);
      desc->BackFace.StencilDepthFailOp =
        translate_stencil_op(in_zsa->back.depthFailOp);
      desc->BackFace.StencilPassOp =
        translate_stencil_op(in_zsa->back.passOp);
      desc->BackFace.StencilFunc =
        dzn_translate_compare_op(in_zsa->back.compareOp);

      pipeline->zsa.stencil_test.enable = true;

      translate_stencil_test(pipeline, desc, in);
   }
}

static D3D12_BLEND
translate_blend_factor(VkBlendFactor in, bool is_alpha)
{
   switch (in) {
   case VK_BLEND_FACTOR_ZERO: return D3D12_BLEND_ZERO;
   case VK_BLEND_FACTOR_ONE: return D3D12_BLEND_ONE;
   case VK_BLEND_FACTOR_SRC_COLOR:
      return is_alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return is_alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
   case VK_BLEND_FACTOR_DST_COLOR:
      return is_alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
      return is_alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
   case VK_BLEND_FACTOR_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
   case VK_BLEND_FACTOR_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
   /* FIXME: no way to isolate the alpla and color constants */
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
      return D3D12_BLEND_BLEND_FACTOR;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return D3D12_BLEND_INV_BLEND_FACTOR;
   case VK_BLEND_FACTOR_SRC1_COLOR:
      return is_alpha ? D3D12_BLEND_SRC1_ALPHA : D3D12_BLEND_SRC1_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return is_alpha ? D3D12_BLEND_INV_SRC1_ALPHA : D3D12_BLEND_INV_SRC1_COLOR;
   case VK_BLEND_FACTOR_SRC1_ALPHA: return D3D12_BLEND_SRC1_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return D3D12_BLEND_INV_SRC1_ALPHA;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE: return D3D12_BLEND_SRC_ALPHA_SAT;
   default: unreachable("Invalid blend factor");
   }
}

static D3D12_BLEND_OP
translate_blend_op(VkBlendOp in)
{
   switch (in) {
   case VK_BLEND_OP_ADD: return D3D12_BLEND_OP_ADD;
   case VK_BLEND_OP_SUBTRACT: return D3D12_BLEND_OP_SUBTRACT;
   case VK_BLEND_OP_REVERSE_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
   case VK_BLEND_OP_MIN: return D3D12_BLEND_OP_MIN;
   case VK_BLEND_OP_MAX: return D3D12_BLEND_OP_MAX;
   default: unreachable("Invalid blend op");
   }
}

static D3D12_LOGIC_OP
translate_logic_op(VkLogicOp in)
{
   switch (in) {
   case VK_LOGIC_OP_CLEAR: return D3D12_LOGIC_OP_CLEAR;
   case VK_LOGIC_OP_AND: return D3D12_LOGIC_OP_AND;
   case VK_LOGIC_OP_AND_REVERSE: return D3D12_LOGIC_OP_AND_REVERSE;
   case VK_LOGIC_OP_COPY: return D3D12_LOGIC_OP_COPY;
   case VK_LOGIC_OP_AND_INVERTED: return D3D12_LOGIC_OP_AND_INVERTED;
   case VK_LOGIC_OP_NO_OP: return D3D12_LOGIC_OP_NOOP;
   case VK_LOGIC_OP_XOR: return D3D12_LOGIC_OP_XOR;
   case VK_LOGIC_OP_OR: return D3D12_LOGIC_OP_OR;
   case VK_LOGIC_OP_NOR: return D3D12_LOGIC_OP_NOR;
   case VK_LOGIC_OP_EQUIVALENT: return D3D12_LOGIC_OP_EQUIV;
   case VK_LOGIC_OP_INVERT: return D3D12_LOGIC_OP_INVERT;
   case VK_LOGIC_OP_OR_REVERSE: return D3D12_LOGIC_OP_OR_REVERSE;
   case VK_LOGIC_OP_COPY_INVERTED: return D3D12_LOGIC_OP_COPY_INVERTED;
   case VK_LOGIC_OP_OR_INVERTED: return D3D12_LOGIC_OP_OR_INVERTED;
   case VK_LOGIC_OP_NAND: return D3D12_LOGIC_OP_NAND;
   case VK_LOGIC_OP_SET: return D3D12_LOGIC_OP_SET;
   default: unreachable("Invalid logic op");
   }
}

static void
dzn_graphics_pipeline_translate_blend(struct dzn_graphics_pipeline *pipeline,
                                      D3D12_PIPELINE_STATE_STREAM_DESC *out,
                                      const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineColorBlendStateCreateInfo *in_blend =
      in_rast->rasterizerDiscardEnable ? NULL : in->pColorBlendState;
   const VkPipelineMultisampleStateCreateInfo *in_ms =
      in_rast->rasterizerDiscardEnable ? NULL : in->pMultisampleState;

   if (!in_blend || !in_ms)
      return;

   d3d12_gfx_pipeline_state_stream_new_desc(out, BLEND, D3D12_BLEND_DESC, desc);
   D3D12_LOGIC_OP logicop =
      in_blend->logicOpEnable ?
      translate_logic_op(in_blend->logicOp) : D3D12_LOGIC_OP_NOOP;
   desc->AlphaToCoverageEnable = in_ms->alphaToCoverageEnable;
   memcpy(pipeline->blend.constants, in_blend->blendConstants,
          sizeof(pipeline->blend.constants));

   for (uint32_t i = 0; i < in_blend->attachmentCount; i++) {
      if (i > 0 &&
          !memcmp(&in_blend->pAttachments[i - 1], &in_blend->pAttachments[i],
                  sizeof(*in_blend->pAttachments)))
         desc->IndependentBlendEnable = true;

      desc->RenderTarget[i].BlendEnable =
         in_blend->pAttachments[i].blendEnable;
      desc->RenderTarget[i].RenderTargetWriteMask =
         in_blend->pAttachments[i].colorWriteMask;

      if (in_blend->logicOpEnable) {
         desc->RenderTarget[i].LogicOpEnable = true;
         desc->RenderTarget[i].LogicOp = logicop;
      } else {
         desc->RenderTarget[i].SrcBlend =
            translate_blend_factor(in_blend->pAttachments[i].srcColorBlendFactor, false);
         desc->RenderTarget[i].DestBlend =
            translate_blend_factor(in_blend->pAttachments[i].dstColorBlendFactor, false);
         desc->RenderTarget[i].BlendOp =
            translate_blend_op(in_blend->pAttachments[i].colorBlendOp);
         desc->RenderTarget[i].SrcBlendAlpha =
            translate_blend_factor(in_blend->pAttachments[i].srcAlphaBlendFactor, true);
         desc->RenderTarget[i].DestBlendAlpha =
            translate_blend_factor(in_blend->pAttachments[i].dstAlphaBlendFactor, true);
         desc->RenderTarget[i].BlendOpAlpha =
            translate_blend_op(in_blend->pAttachments[i].alphaBlendOp);
      }
   }
}


static void
dzn_pipeline_init(struct dzn_pipeline *pipeline,
                  struct dzn_device *device,
                  VkPipelineBindPoint type,
                  struct dzn_pipeline_layout *layout,
                  D3D12_PIPELINE_STATE_STREAM_DESC *stream_desc)
{
   pipeline->type = type;
   pipeline->root.sets_param_count = layout->root.sets_param_count;
   pipeline->root.sysval_cbv_param_idx = layout->root.sysval_cbv_param_idx;
   pipeline->root.push_constant_cbv_param_idx = layout->root.push_constant_cbv_param_idx;
   STATIC_ASSERT(sizeof(pipeline->root.type) == sizeof(layout->root.type));
   memcpy(pipeline->root.type, layout->root.type, sizeof(pipeline->root.type));
   pipeline->root.sig = layout->root.sig;
   ID3D12RootSignature_AddRef(pipeline->root.sig);

   STATIC_ASSERT(sizeof(layout->desc_count) == sizeof(pipeline->desc_count));
   memcpy(pipeline->desc_count, layout->desc_count, sizeof(pipeline->desc_count));

   STATIC_ASSERT(sizeof(layout->sets) == sizeof(pipeline->sets));
   memcpy(pipeline->sets, layout->sets, sizeof(pipeline->sets));
   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);

   ASSERTED uint32_t max_streamsz =
      type == VK_PIPELINE_BIND_POINT_GRAPHICS ?
      MAX_GFX_PIPELINE_STATE_STREAM_SIZE :
      MAX_COMPUTE_PIPELINE_STATE_STREAM_SIZE;

   d3d12_pipeline_state_stream_new_desc(stream_desc, max_streamsz, ROOT_SIGNATURE,
                                        ID3D12RootSignature *, root_sig);
   *root_sig = pipeline->root.sig;
}

static void
dzn_pipeline_finish(struct dzn_pipeline *pipeline)
{
   if (pipeline->state)
      ID3D12PipelineState_Release(pipeline->state);
   if (pipeline->root.sig)
      ID3D12RootSignature_Release(pipeline->root.sig);

   vk_object_base_finish(&pipeline->base);
}

static void dzn_graphics_pipeline_delete_variant(struct hash_entry *he)
{
   struct dzn_graphics_pipeline_variant *variant = he->data;

   if (variant->state)
      ID3D12PipelineState_Release(variant->state);
}

static void
dzn_graphics_pipeline_cleanup_nir_shaders(struct dzn_graphics_pipeline *pipeline)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(pipeline->templates.shaders); i++) {
      ralloc_free(pipeline->templates.shaders[i].nir);
      pipeline->templates.shaders[i].nir = NULL;
   }
}

static void
dzn_graphics_pipeline_cleanup_dxil_shaders(struct dzn_graphics_pipeline *pipeline)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(pipeline->templates.shaders); i++) {
      if (pipeline->templates.shaders[i].bc) {
         free((void *)pipeline->templates.shaders[i].bc->pShaderBytecode);
         pipeline->templates.shaders[i].bc = NULL;
      }
   }
}

static void
dzn_graphics_pipeline_destroy(struct dzn_graphics_pipeline *pipeline,
                              const VkAllocationCallbacks *alloc)
{
   if (!pipeline)
      return;

   _mesa_hash_table_destroy(pipeline->variants,
                            dzn_graphics_pipeline_delete_variant);

   dzn_graphics_pipeline_cleanup_nir_shaders(pipeline);
   dzn_graphics_pipeline_cleanup_dxil_shaders(pipeline);

   for (uint32_t i = 0; i < ARRAY_SIZE(pipeline->indirect_cmd_sigs); i++) {
      if (pipeline->indirect_cmd_sigs[i])
         ID3D12CommandSignature_Release(pipeline->indirect_cmd_sigs[i]);
   }

   dzn_pipeline_finish(&pipeline->base);
   vk_free2(&pipeline->base.base.device->alloc, alloc, pipeline);
}

static VkResult
dzn_graphics_pipeline_create(struct dzn_device *device,
                             VkPipelineCache cache,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkPipeline *out)
{
   const VkPipelineRenderingCreateInfo *ri = (const VkPipelineRenderingCreateInfo *)
      vk_find_struct_const(pCreateInfo, PIPELINE_RENDERING_CREATE_INFO);
   VK_FROM_HANDLE(vk_pipeline_cache, pcache, cache);
   VK_FROM_HANDLE(vk_render_pass, pass, pCreateInfo->renderPass);
   VK_FROM_HANDLE(dzn_pipeline_layout, layout, pCreateInfo->layout);
   uint32_t color_count = 0;
   VkFormat color_fmts[MAX_RTS] = { 0 };
   VkFormat zs_fmt = VK_FORMAT_UNDEFINED;
   VkResult ret;
   HRESULT hres = 0;

   struct dzn_graphics_pipeline *pipeline =
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   D3D12_PIPELINE_STATE_STREAM_DESC *stream_desc = &pipeline->templates.stream_desc;
   stream_desc->pPipelineStateSubobjectStream = pipeline->templates.stream_buf;

   dzn_pipeline_init(&pipeline->base, device,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     layout, stream_desc);
   D3D12_INPUT_ELEMENT_DESC attribs[MAX_VERTEX_GENERIC_ATTRIBS] = { 0 };
   enum pipe_format vi_conversions[MAX_VERTEX_GENERIC_ATTRIBS] = { 0 };

   ret = dzn_graphics_pipeline_translate_vi(pipeline, pCreateInfo,
                                            attribs, vi_conversions);
   if (ret != VK_SUCCESS)
      goto out;

   if (pCreateInfo->pDynamicState) {
      for (uint32_t i = 0; i < pCreateInfo->pDynamicState->dynamicStateCount; i++) {
         switch (pCreateInfo->pDynamicState->pDynamicStates[i]) {
         case VK_DYNAMIC_STATE_VIEWPORT:
            pipeline->vp.dynamic = true;
            break;
         case VK_DYNAMIC_STATE_SCISSOR:
            pipeline->scissor.dynamic = true;
            break;
         case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
            pipeline->zsa.stencil_test.dynamic_ref = true;
            break;
         case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
            pipeline->zsa.stencil_test.dynamic_compare_mask = true;
            break;
         case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
            pipeline->zsa.stencil_test.dynamic_write_mask = true;
            break;
         case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
            pipeline->blend.dynamic_constants = true;
            break;
         case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
            pipeline->zsa.depth_bounds.dynamic = true;
            break;
         case VK_DYNAMIC_STATE_DEPTH_BIAS:
            pipeline->zsa.dynamic_depth_bias = true;
            ret = dzn_graphics_pipeline_prepare_for_variants(device, pipeline);
            if (ret)
               goto out;
            break;
         case VK_DYNAMIC_STATE_LINE_WIDTH:
            /* Nothing to do since we just support lineWidth = 1. */
            break;
         default: unreachable("Unsupported dynamic state");
         }
      }
   }

   ret = dzn_graphics_pipeline_translate_ia(device, pipeline, stream_desc, pCreateInfo);
   if (ret)
      goto out;

   dzn_graphics_pipeline_translate_rast(pipeline, stream_desc, pCreateInfo);
   dzn_graphics_pipeline_translate_ms(pipeline, stream_desc, pCreateInfo);
   dzn_graphics_pipeline_translate_zsa(pipeline, stream_desc, pCreateInfo);
   dzn_graphics_pipeline_translate_blend(pipeline, stream_desc, pCreateInfo);

   if (pass) {
      const struct vk_subpass *subpass = &pass->subpasses[pCreateInfo->subpass];
      color_count = subpass->color_count;
      for (uint32_t i = 0; i < subpass->color_count; i++) {
         uint32_t idx = subpass->color_attachments[i].attachment;

         if (idx == VK_ATTACHMENT_UNUSED) continue;

         const struct vk_render_pass_attachment *attachment =
            &pass->attachments[idx];

         color_fmts[i] = attachment->format;
      }

      if (subpass->depth_stencil_attachment &&
          subpass->depth_stencil_attachment->attachment != VK_ATTACHMENT_UNUSED) {
         const struct vk_render_pass_attachment *attachment =
            &pass->attachments[subpass->depth_stencil_attachment->attachment];

         zs_fmt = attachment->format;
      }
   } else if (ri) {
      color_count = ri->colorAttachmentCount;
      memcpy(color_fmts, ri->pColorAttachmentFormats,
             sizeof(color_fmts[0]) * color_count);
      if (ri->depthAttachmentFormat != VK_FORMAT_UNDEFINED)
         zs_fmt = ri->depthAttachmentFormat;
      else if (ri->stencilAttachmentFormat != VK_FORMAT_UNDEFINED)
         zs_fmt = ri->stencilAttachmentFormat;
   }

   if (color_count > 0) {
      d3d12_gfx_pipeline_state_stream_new_desc(stream_desc, RENDER_TARGET_FORMATS, struct D3D12_RT_FORMAT_ARRAY, rts);
      rts->NumRenderTargets = color_count;
      for (uint32_t i = 0; i < color_count; i++) {
         rts->RTFormats[i] =
            dzn_image_get_dxgi_format(color_fmts[i],
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                      VK_IMAGE_ASPECT_COLOR_BIT);
      }
   }

   if (zs_fmt != VK_FORMAT_UNDEFINED) {
      d3d12_gfx_pipeline_state_stream_new_desc(stream_desc, DEPTH_STENCIL_FORMAT, DXGI_FORMAT, ds_fmt);
      *ds_fmt =
         dzn_image_get_dxgi_format(zs_fmt,
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT |
                                   VK_IMAGE_ASPECT_STENCIL_BIT);
   }

   ret = dzn_graphics_pipeline_compile_shaders(device, pipeline, pcache,
                                               layout, stream_desc,
                                               attribs, vi_conversions,
                                               pCreateInfo);
   if (ret != VK_SUCCESS)
      goto out;

   if (!pipeline->variants) {
      hres = ID3D12Device2_CreatePipelineState(device->dev, stream_desc,
                                               &IID_ID3D12PipelineState,
                                               (void **)&pipeline->base.state);
      if (FAILED(hres)) {
         ret = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto out;
      }

      dzn_graphics_pipeline_cleanup_dxil_shaders(pipeline);
   }

   dzn_graphics_pipeline_cleanup_nir_shaders(pipeline);
   ret = VK_SUCCESS;

out:
   if (ret != VK_SUCCESS)
      dzn_graphics_pipeline_destroy(pipeline, pAllocator);
   else
      *out = dzn_graphics_pipeline_to_handle(pipeline);

   return ret;
}

ID3D12PipelineState *
dzn_graphics_pipeline_get_state(struct dzn_graphics_pipeline *pipeline,
                                const struct dzn_graphics_pipeline_variant_key *key)
{
   if (!pipeline->variants)
      return pipeline->base.state;

   struct dzn_graphics_pipeline_variant_key masked_key = { 0 };

   if (dzn_graphics_pipeline_get_desc_template(pipeline, ib_strip_cut))
      masked_key.ib_strip_cut = key->ib_strip_cut;

   if (dzn_graphics_pipeline_get_desc_template(pipeline, rast) &&
       pipeline->zsa.dynamic_depth_bias)
      masked_key.depth_bias = key->depth_bias;

   const D3D12_DEPTH_STENCIL_DESC1 *ds_templ =
      dzn_graphics_pipeline_get_desc_template(pipeline, ds);
   if (ds_templ && ds_templ->StencilEnable) {
      if (ds_templ->FrontFace.StencilFunc != D3D12_COMPARISON_FUNC_NEVER &&
         ds_templ->FrontFace.StencilFunc != D3D12_COMPARISON_FUNC_ALWAYS)
         masked_key.stencil_test.front.compare_mask = key->stencil_test.front.compare_mask;
      if (ds_templ->BackFace.StencilFunc != D3D12_COMPARISON_FUNC_NEVER &&
          ds_templ->BackFace.StencilFunc != D3D12_COMPARISON_FUNC_ALWAYS)
         masked_key.stencil_test.back.compare_mask = key->stencil_test.back.compare_mask;
      if (pipeline->zsa.stencil_test.dynamic_write_mask) {
         masked_key.stencil_test.front.write_mask = key->stencil_test.front.write_mask;
         masked_key.stencil_test.back.write_mask = key->stencil_test.back.write_mask;
      }
   }

   struct dzn_device *device =
      container_of(pipeline->base.base.device, struct dzn_device, vk);
   struct hash_entry *he =
      _mesa_hash_table_search(pipeline->variants, &masked_key);

   struct dzn_graphics_pipeline_variant *variant;

   if (!he) {
      variant = rzalloc(pipeline->variants, struct dzn_graphics_pipeline_variant);
      variant->key = masked_key;

      uintptr_t stream_buf[MAX_GFX_PIPELINE_STATE_STREAM_SIZE / sizeof(uintptr_t)];
      D3D12_PIPELINE_STATE_STREAM_DESC stream_desc = {
         .SizeInBytes = pipeline->templates.stream_desc.SizeInBytes,
         .pPipelineStateSubobjectStream = stream_buf,
      };

      memcpy(stream_buf, pipeline->templates.stream_buf, stream_desc.SizeInBytes);

      D3D12_INDEX_BUFFER_STRIP_CUT_VALUE *ib_strip_cut =
         dzn_graphics_pipeline_get_desc(pipeline, stream_buf, ib_strip_cut);
      if (ib_strip_cut)
         *ib_strip_cut = masked_key.ib_strip_cut;

      D3D12_RASTERIZER_DESC *rast =
         dzn_graphics_pipeline_get_desc(pipeline, stream_buf, rast);
      if (rast && pipeline->zsa.dynamic_depth_bias) {
         rast->DepthBias = translate_depth_bias(masked_key.depth_bias.constant_factor);
         rast->DepthBiasClamp = masked_key.depth_bias.clamp;
         rast->SlopeScaledDepthBias = masked_key.depth_bias.slope_factor;
      }

      D3D12_DEPTH_STENCIL_DESC1 *ds =
         dzn_graphics_pipeline_get_desc(pipeline, stream_buf, ds);
      if (ds && ds->StencilEnable) {
         if (pipeline->zsa.stencil_test.dynamic_compare_mask) {
            if (ds->FrontFace.StencilFunc != D3D12_COMPARISON_FUNC_NEVER &&
                ds->FrontFace.StencilFunc != D3D12_COMPARISON_FUNC_ALWAYS) {
               ds->StencilReadMask = masked_key.stencil_test.front.compare_mask;
            }

            if (ds->BackFace.StencilFunc != D3D12_COMPARISON_FUNC_NEVER &&
                ds->BackFace.StencilFunc != D3D12_COMPARISON_FUNC_ALWAYS) {
               ds->StencilReadMask = masked_key.stencil_test.back.compare_mask;
            }

            if (ds->FrontFace.StencilFunc != D3D12_COMPARISON_FUNC_NEVER &&
                ds->FrontFace.StencilFunc != D3D12_COMPARISON_FUNC_ALWAYS &&
                ds->BackFace.StencilFunc != D3D12_COMPARISON_FUNC_NEVER &&
                ds->BackFace.StencilFunc != D3D12_COMPARISON_FUNC_ALWAYS)
               assert(masked_key.stencil_test.front.compare_mask == masked_key.stencil_test.back.compare_mask);
         }

         if (pipeline->zsa.stencil_test.dynamic_write_mask) {
            assert(!masked_key.stencil_test.front.write_mask ||
                   !masked_key.stencil_test.back.write_mask ||
                   masked_key.stencil_test.front.write_mask == masked_key.stencil_test.back.write_mask);
            ds->StencilWriteMask =
               masked_key.stencil_test.front.write_mask |
               masked_key.stencil_test.back.write_mask;
         }
      }

      ASSERTED HRESULT hres = ID3D12Device2_CreatePipelineState(device->dev, &stream_desc,
                                                                &IID_ID3D12PipelineState,
                                                                (void**)(&variant->state));
      assert(!FAILED(hres));
      he = _mesa_hash_table_insert(pipeline->variants, &variant->key, variant);
      assert(he);
   } else {
      variant = he->data;
   }

   if (variant->state)
      ID3D12PipelineState_AddRef(variant->state);

   if (pipeline->base.state)
      ID3D12PipelineState_Release(pipeline->base.state);

   pipeline->base.state = variant->state;
   return variant->state;
}

#define DZN_INDIRECT_CMD_SIG_MAX_ARGS 4

ID3D12CommandSignature *
dzn_graphics_pipeline_get_indirect_cmd_sig(struct dzn_graphics_pipeline *pipeline,
                                           enum dzn_indirect_draw_cmd_sig_type type)
{
   assert(type < DZN_NUM_INDIRECT_DRAW_CMD_SIGS);

   struct dzn_device *device =
      container_of(pipeline->base.base.device, struct dzn_device, vk);
   ID3D12CommandSignature *cmdsig = pipeline->indirect_cmd_sigs[type];

   if (cmdsig)
      return cmdsig;

   bool triangle_fan = type == DZN_INDIRECT_DRAW_TRIANGLE_FAN_CMD_SIG;
   bool indexed = type == DZN_INDIRECT_INDEXED_DRAW_CMD_SIG || triangle_fan;

   uint32_t cmd_arg_count = 0;
   D3D12_INDIRECT_ARGUMENT_DESC cmd_args[DZN_INDIRECT_CMD_SIG_MAX_ARGS];

   if (triangle_fan) {
      cmd_args[cmd_arg_count++] = (D3D12_INDIRECT_ARGUMENT_DESC) {
         .Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW,
      };
   }

   cmd_args[cmd_arg_count++] = (D3D12_INDIRECT_ARGUMENT_DESC) {
      .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
      .Constant = {
         .RootParameterIndex = pipeline->base.root.sysval_cbv_param_idx,
         .DestOffsetIn32BitValues = offsetof(struct dxil_spirv_vertex_runtime_data, first_vertex) / 4,
         .Num32BitValuesToSet = 2,
      },
   };

   cmd_args[cmd_arg_count++] = (D3D12_INDIRECT_ARGUMENT_DESC) {
      .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
      .Constant = {
         .RootParameterIndex = pipeline->base.root.sysval_cbv_param_idx,
         .DestOffsetIn32BitValues = offsetof(struct dxil_spirv_vertex_runtime_data, draw_id) / 4,
         .Num32BitValuesToSet = 1,
      },
   };

   cmd_args[cmd_arg_count++] = (D3D12_INDIRECT_ARGUMENT_DESC) {
      .Type = indexed ?
              D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED :
              D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
   };

   assert(cmd_arg_count <= ARRAY_SIZE(cmd_args));
   assert(offsetof(struct dxil_spirv_vertex_runtime_data, first_vertex) == 0);

   D3D12_COMMAND_SIGNATURE_DESC cmd_sig_desc = {
      .ByteStride =
         triangle_fan ?
         sizeof(struct dzn_indirect_triangle_fan_draw_exec_params) :
         sizeof(struct dzn_indirect_draw_exec_params),
      .NumArgumentDescs = cmd_arg_count,
      .pArgumentDescs = cmd_args,
   };
   HRESULT hres =
      ID3D12Device1_CreateCommandSignature(device->dev, &cmd_sig_desc,
                                           pipeline->base.root.sig,
                                           &IID_ID3D12CommandSignature,
                                           (void **)&cmdsig);
   if (FAILED(hres))
      return NULL;

   pipeline->indirect_cmd_sigs[type] = cmdsig;
   return cmdsig;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateGraphicsPipelines(VkDevice dev,
                            VkPipelineCache pipelineCache,
                            uint32_t count,
                            const VkGraphicsPipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VkResult result = VK_SUCCESS;

   unsigned i;
   for (i = 0; i < count; i++) {
      result = dzn_graphics_pipeline_create(device,
                                            pipelineCache,
                                            &pCreateInfos[i],
                                            pAllocator,
                                            &pPipelines[i]);
      if (result != VK_SUCCESS) {
         pPipelines[i] = VK_NULL_HANDLE;

         /* Bail out on the first error != VK_PIPELINE_COMPILE_REQUIRED_EX as it
          * is not obvious what error should be report upon 2 different failures.
          */
         if (result != VK_PIPELINE_COMPILE_REQUIRED)
            break;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

static void
dzn_compute_pipeline_destroy(struct dzn_compute_pipeline *pipeline,
                             const VkAllocationCallbacks *alloc)
{
   if (!pipeline)
      return;

   if (pipeline->indirect_cmd_sig)
      ID3D12CommandSignature_Release(pipeline->indirect_cmd_sig);

   dzn_pipeline_finish(&pipeline->base);
   vk_free2(&pipeline->base.base.device->alloc, alloc, pipeline);
}

static VkResult
dzn_pipeline_cache_lookup_compute_pipeline(struct vk_pipeline_cache *cache,
                                           uint8_t *pipeline_hash,
                                           D3D12_PIPELINE_STATE_STREAM_DESC *stream_desc,
                                           D3D12_SHADER_BYTECODE *dxil,
                                           bool *cache_hit)
{
   *cache_hit = false;

   if (!cache)
      return VK_SUCCESS;

   struct vk_pipeline_cache_object *cache_obj = NULL;

   cache_obj =
      vk_pipeline_cache_lookup_object(cache, pipeline_hash, SHA1_DIGEST_LENGTH,
                                      &dzn_cached_blob_ops,
                                      NULL);
   if (!cache_obj)
      return VK_SUCCESS;

   struct dzn_cached_blob *cached_blob =
      container_of(cache_obj, struct dzn_cached_blob, base);

   assert(cached_blob->size == SHA1_DIGEST_LENGTH);

   const uint8_t *dxil_hash = cached_blob->data;
   gl_shader_stage stage;

   VkResult ret =
      dzn_pipeline_cache_lookup_dxil_shader(cache, dxil_hash, &stage, dxil);

   if (ret != VK_SUCCESS || stage == MESA_SHADER_NONE)
      goto out;

   assert(stage == MESA_SHADER_COMPUTE);

   d3d12_compute_pipeline_state_stream_new_desc(stream_desc, CS, D3D12_SHADER_BYTECODE, slot);
   *slot = *dxil;
   *cache_hit = true;

out:
   vk_pipeline_cache_object_unref(cache_obj);
   return ret;
}

static void
dzn_pipeline_cache_add_compute_pipeline(struct vk_pipeline_cache *cache,
                                        uint8_t *pipeline_hash,
                                        uint8_t *dxil_hash)
{
   struct vk_pipeline_cache_object *cache_obj =
      dzn_cached_blob_create(cache->base.device, pipeline_hash, NULL, SHA1_DIGEST_LENGTH);
   if (!cache_obj)
      return;

   struct dzn_cached_blob *cached_blob =
      container_of(cache_obj, struct dzn_cached_blob, base);

   memcpy((void *)cached_blob->data, dxil_hash, SHA1_DIGEST_LENGTH);

   cache_obj = vk_pipeline_cache_add_object(cache, cache_obj);
   vk_pipeline_cache_object_unref(cache_obj);
}

static VkResult
dzn_compute_pipeline_compile_shader(struct dzn_device *device,
                                    struct dzn_compute_pipeline *pipeline,
                                    struct vk_pipeline_cache *cache,
                                    const struct dzn_pipeline_layout *layout,
                                    D3D12_PIPELINE_STATE_STREAM_DESC *stream_desc,
                                    D3D12_SHADER_BYTECODE *shader,
                                    const VkComputePipelineCreateInfo *info)
{
   uint8_t spirv_hash[SHA1_DIGEST_LENGTH], pipeline_hash[SHA1_DIGEST_LENGTH];
   VkResult ret = VK_SUCCESS;
   nir_shader *nir = NULL;

   if (cache) {
      struct mesa_sha1 pipeline_hash_ctx;

      _mesa_sha1_init(&pipeline_hash_ctx);
      vk_pipeline_hash_shader_stage(&info->stage, spirv_hash);
      _mesa_sha1_update(&pipeline_hash_ctx, spirv_hash, sizeof(spirv_hash));
      _mesa_sha1_update(&pipeline_hash_ctx, layout->stages[MESA_SHADER_COMPUTE].hash,
                        sizeof(layout->stages[MESA_SHADER_COMPUTE].hash));
      _mesa_sha1_final(&pipeline_hash_ctx, pipeline_hash);

      bool cache_hit = false;
      ret = dzn_pipeline_cache_lookup_compute_pipeline(cache, pipeline_hash,
                                                       stream_desc, shader,
                                                       &cache_hit);
      if (ret != VK_SUCCESS || cache_hit)
         goto out;
   }

   ret = dzn_pipeline_get_nir_shader(device, layout, cache, spirv_hash,
                                     &info->stage, MESA_SHADER_COMPUTE,
                                     DXIL_SPIRV_YZ_FLIP_NONE, 0, 0,
                                     false, NULL,
                                     dxil_get_nir_compiler_options(), &nir);
   if (ret != VK_SUCCESS)
      return ret;

   uint8_t bindings_hash[SHA1_DIGEST_LENGTH], dxil_hash[SHA1_DIGEST_LENGTH];

   NIR_PASS_V(nir, adjust_var_bindings, layout, cache ? bindings_hash : NULL);

   if (cache) {
      struct mesa_sha1 dxil_hash_ctx;

      _mesa_sha1_init(&dxil_hash_ctx);
      _mesa_sha1_update(&dxil_hash_ctx, spirv_hash, sizeof(spirv_hash));
      _mesa_sha1_update(&dxil_hash_ctx, bindings_hash, sizeof(bindings_hash));
      _mesa_sha1_final(&dxil_hash_ctx, dxil_hash);

      gl_shader_stage stage;

      ret = dzn_pipeline_cache_lookup_dxil_shader(cache, dxil_hash, &stage, shader);
      if (ret != VK_SUCCESS)
         goto out;

      if (stage != MESA_SHADER_NONE) {
         assert(stage == MESA_SHADER_COMPUTE);
         d3d12_compute_pipeline_state_stream_new_desc(stream_desc, CS, D3D12_SHADER_BYTECODE, cs);
         *cs = *shader;
         dzn_pipeline_cache_add_compute_pipeline(cache, pipeline_hash, dxil_hash);
         goto out;
      }
   }

   ret = dzn_pipeline_compile_shader(device, nir, shader);
   if (ret != VK_SUCCESS)
      goto out;

   d3d12_compute_pipeline_state_stream_new_desc(stream_desc, CS, D3D12_SHADER_BYTECODE, cs);
   *cs = *shader;

   if (cache) {
      dzn_pipeline_cache_add_dxil_shader(cache, dxil_hash, MESA_SHADER_COMPUTE, shader);
      dzn_pipeline_cache_add_compute_pipeline(cache, pipeline_hash, dxil_hash);
   }

out:
   ralloc_free(nir);
   return ret;
}

static VkResult
dzn_compute_pipeline_create(struct dzn_device *device,
                            VkPipelineCache cache,
                            const VkComputePipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *out)
{
   VK_FROM_HANDLE(dzn_pipeline_layout, layout, pCreateInfo->layout);
   VK_FROM_HANDLE(vk_pipeline_cache, pcache, cache);

   struct dzn_compute_pipeline *pipeline =
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uintptr_t state_buf[MAX_COMPUTE_PIPELINE_STATE_STREAM_SIZE / sizeof(uintptr_t)];
   D3D12_PIPELINE_STATE_STREAM_DESC stream_desc = {
      .pPipelineStateSubobjectStream = state_buf,
   };

   dzn_pipeline_init(&pipeline->base, device,
                     VK_PIPELINE_BIND_POINT_COMPUTE,
                     layout, &stream_desc);

   D3D12_SHADER_BYTECODE shader = { 0 };
   VkResult ret =
      dzn_compute_pipeline_compile_shader(device, pipeline, pcache, layout,
                                          &stream_desc, &shader, pCreateInfo);
   if (ret != VK_SUCCESS)
      goto out;

   if (FAILED(ID3D12Device2_CreatePipelineState(device->dev, &stream_desc,
                                                &IID_ID3D12PipelineState,
                                                (void **)&pipeline->base.state)))
      ret = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

out:
   free((void *)shader.pShaderBytecode);
   if (ret != VK_SUCCESS)
      dzn_compute_pipeline_destroy(pipeline, pAllocator);
   else
      *out = dzn_compute_pipeline_to_handle(pipeline);

   return ret;
}

ID3D12CommandSignature *
dzn_compute_pipeline_get_indirect_cmd_sig(struct dzn_compute_pipeline *pipeline)
{
   if (pipeline->indirect_cmd_sig)
      return pipeline->indirect_cmd_sig;

   struct dzn_device *device =
      container_of(pipeline->base.base.device, struct dzn_device, vk);

   D3D12_INDIRECT_ARGUMENT_DESC indirect_dispatch_args[] = {
      {
         .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
         .Constant = {
            .RootParameterIndex = pipeline->base.root.sysval_cbv_param_idx,
            .DestOffsetIn32BitValues = 0,
            .Num32BitValuesToSet = 3,
         },
      },
      {
         .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
      },
   };

   D3D12_COMMAND_SIGNATURE_DESC indirect_dispatch_desc = {
      .ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS) * 2,
      .NumArgumentDescs = ARRAY_SIZE(indirect_dispatch_args),
      .pArgumentDescs = indirect_dispatch_args,
   };

   HRESULT hres =
      ID3D12Device1_CreateCommandSignature(device->dev, &indirect_dispatch_desc,
                                           pipeline->base.root.sig,
                                           &IID_ID3D12CommandSignature,
                                           (void **)&pipeline->indirect_cmd_sig);
   if (FAILED(hres))
      return NULL;

   return pipeline->indirect_cmd_sig;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateComputePipelines(VkDevice dev,
                           VkPipelineCache pipelineCache,
                           uint32_t count,
                           const VkComputePipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VkResult result = VK_SUCCESS;

   unsigned i;
   for (i = 0; i < count; i++) {
      result = dzn_compute_pipeline_create(device,
                                           pipelineCache,
                                           &pCreateInfos[i],
                                           pAllocator,
                                           &pPipelines[i]);
      if (result != VK_SUCCESS) {
         pPipelines[i] = VK_NULL_HANDLE;

         /* Bail out on the first error != VK_PIPELINE_COMPILE_REQUIRED_EX as it
          * is not obvious what error should be report upon 2 different failures.
          */
         if (result != VK_PIPELINE_COMPILE_REQUIRED)
            break;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyPipeline(VkDevice device,
                    VkPipeline pipeline,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(dzn_pipeline, pipe, pipeline);

   if (!pipe)
      return;

   if (pipe->type == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      struct dzn_graphics_pipeline *gfx = container_of(pipe, struct dzn_graphics_pipeline, base);
      dzn_graphics_pipeline_destroy(gfx, pAllocator);
   } else {
      assert(pipe->type == VK_PIPELINE_BIND_POINT_COMPUTE);
      struct dzn_compute_pipeline *compute = container_of(pipe, struct dzn_compute_pipeline, base);
      dzn_compute_pipeline_destroy(compute, pAllocator);
   }
}
