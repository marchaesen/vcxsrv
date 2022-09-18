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

#include "vk_alloc.h"
#include "vk_descriptors.h"
#include "vk_util.h"

#include "util/mesa-sha1.h"

static uint32_t
translate_desc_stages(VkShaderStageFlags in)
{
   if (in == VK_SHADER_STAGE_ALL)
      in = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;

   uint32_t out = 0;

   u_foreach_bit(s, in)
      out |= BITFIELD_BIT(vk_to_mesa_shader_stage(BITFIELD_BIT(s)));

   return out;
}

static D3D12_SHADER_VISIBILITY
translate_desc_visibility(VkShaderStageFlags in)
{
   switch (in) {
   case VK_SHADER_STAGE_VERTEX_BIT: return D3D12_SHADER_VISIBILITY_VERTEX;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return D3D12_SHADER_VISIBILITY_HULL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return D3D12_SHADER_VISIBILITY_DOMAIN;
   case VK_SHADER_STAGE_GEOMETRY_BIT: return D3D12_SHADER_VISIBILITY_GEOMETRY;
   case VK_SHADER_STAGE_FRAGMENT_BIT: return D3D12_SHADER_VISIBILITY_PIXEL;
   default: return D3D12_SHADER_VISIBILITY_ALL;
   }
}

static D3D12_DESCRIPTOR_RANGE_TYPE
desc_type_to_range_type(VkDescriptorType in, bool writeable)
{
   switch (in) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;

   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;

   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return writeable ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
   default:
      unreachable("Unsupported desc type");
   }
}

static bool
is_dynamic_desc_type(VkDescriptorType desc_type)
{
   return (desc_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
           desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
}

static bool
dzn_descriptor_type_depends_on_shader_usage(VkDescriptorType type)
{
   return type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
          type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
          type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
          type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

static inline bool
dzn_desc_type_has_sampler(VkDescriptorType type)
{
   return type == VK_DESCRIPTOR_TYPE_SAMPLER ||
          type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

static uint32_t
num_descs_for_type(VkDescriptorType type, bool static_sampler)
{
   unsigned num_descs = 1;

   /* Some type map to an SRV or UAV depending on how the shaders is using the
    * resource (NONWRITEABLE flag set or not), in that case we need to reserve
    * slots for both the UAV and SRV descs.
    */
   if (dzn_descriptor_type_depends_on_shader_usage(type))
      num_descs++;

   /* There's no combined SRV+SAMPLER type in d3d12, we need an descriptor
    * for the sampler.
    */
   if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      num_descs++;

   /* Don't count immutable samplers, they have their own descriptor. */
   if (static_sampler && dzn_desc_type_has_sampler(type))
      num_descs--;

   return num_descs;
}

static VkResult
dzn_descriptor_set_layout_create(struct dzn_device *device,
                                 const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkDescriptorSetLayout *out)
{
   const VkDescriptorSetLayoutBinding *bindings = pCreateInfo->pBindings;
   uint32_t binding_count = 0, static_sampler_count = 0, total_ranges = 0;
   uint32_t dynamic_ranges_offset = 0, immutable_sampler_count = 0;
   uint32_t range_count[MAX_SHADER_VISIBILITIES][NUM_POOL_TYPES] = { 0 };

   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      binding_count = MAX2(binding_count, bindings[i].binding + 1);

      if (!bindings[i].descriptorCount)
         continue;

      D3D12_SHADER_VISIBILITY visibility =
         translate_desc_visibility(bindings[i].stageFlags);
      VkDescriptorType desc_type = bindings[i].descriptorType;
      bool has_sampler = dzn_desc_type_has_sampler(desc_type);

      /* From the Vulkan 1.1.97 spec for VkDescriptorSetLayoutBinding:
       *
       *    "If descriptorType specifies a VK_DESCRIPTOR_TYPE_SAMPLER or
       *    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER type descriptor, then
       *    pImmutableSamplers can be used to initialize a set of immutable
       *    samplers. [...]  If descriptorType is not one of these descriptor
       *    types, then pImmutableSamplers is ignored.
       *
       * We need to be careful here and only parse pImmutableSamplers if we
       * have one of the right descriptor types.
       */
      bool immutable_samplers =
         has_sampler &&
         bindings[i].pImmutableSamplers != NULL;
      bool static_sampler = false;

      if (immutable_samplers && bindings[i].descriptorCount == 1) {
         VK_FROM_HANDLE(dzn_sampler, sampler, bindings[i].pImmutableSamplers[0]);

         if (sampler->static_border_color != -1)
            static_sampler = true;
      }

      if (static_sampler) {
         static_sampler_count += bindings[i].descriptorCount;
      } else if (has_sampler) {
         range_count[visibility][D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER]++;
         total_ranges++;

         if (immutable_samplers)
            immutable_sampler_count += bindings[i].descriptorCount;
      }

      if (desc_type != VK_DESCRIPTOR_TYPE_SAMPLER) {
         range_count[visibility][D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]++;
         total_ranges++;

         if (dzn_descriptor_type_depends_on_shader_usage(desc_type)) {
            range_count[visibility][D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]++;
            total_ranges++;
         }

         if (!is_dynamic_desc_type(desc_type)) {
            uint32_t factor =
               dzn_descriptor_type_depends_on_shader_usage(desc_type) ? 2 : 1;
            dynamic_ranges_offset += bindings[i].descriptorCount * factor;
         }
      }
   }

   /* We need to allocate decriptor set layouts off the device allocator
    * with DEVICE scope because they are reference counted and may not be
    * destroyed when vkDestroyDescriptorSetLayout is called.
    */
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct dzn_descriptor_set_layout, set_layout, 1);
   VK_MULTIALLOC_DECL(&ma, D3D12_DESCRIPTOR_RANGE1,
                      ranges, total_ranges);
   VK_MULTIALLOC_DECL(&ma, D3D12_STATIC_SAMPLER_DESC, static_samplers,
                      static_sampler_count);
   VK_MULTIALLOC_DECL(&ma, const struct dzn_sampler *, immutable_samplers,
                      immutable_sampler_count);
   VK_MULTIALLOC_DECL(&ma, struct dzn_descriptor_set_layout_binding, binfos,
                      binding_count);

   if (!vk_descriptor_set_layout_multizalloc(&device->vk, &ma))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   set_layout->static_samplers = static_samplers;
   set_layout->static_sampler_count = static_sampler_count;
   set_layout->immutable_samplers = immutable_samplers;
   set_layout->immutable_sampler_count = immutable_sampler_count;
   set_layout->bindings = binfos;
   set_layout->binding_count = binding_count;
   set_layout->dynamic_buffers.range_offset = dynamic_ranges_offset;

   for (uint32_t i = 0; i < MAX_SHADER_VISIBILITIES; i++) {
      dzn_foreach_pool_type (type) {
         if (range_count[i][type]) {
            set_layout->ranges[i][type] = ranges;
            set_layout->range_count[i][type] = range_count[i][type];
            ranges += range_count[i][type];
         }
      }
   }

   VkDescriptorSetLayoutBinding *ordered_bindings;
   VkResult ret =
      vk_create_sorted_bindings(pCreateInfo->pBindings,
                                pCreateInfo->bindingCount,
                                &ordered_bindings);
   if (ret != VK_SUCCESS)
      return ret;

   assert(binding_count ==
          (pCreateInfo->bindingCount ?
           (ordered_bindings[pCreateInfo->bindingCount - 1].binding + 1) : 0));

   uint32_t range_idx[MAX_SHADER_VISIBILITIES][NUM_POOL_TYPES] = { 0 };
   uint32_t static_sampler_idx = 0, immutable_sampler_idx = 0;
   uint32_t dynamic_buffer_idx = 0;
   uint32_t base_register = 0;

   for (uint32_t i = 0; i < binding_count; i++) {
      binfos[i].static_sampler_idx = ~0;
      binfos[i].immutable_sampler_idx = ~0;
      binfos[i].dynamic_buffer_idx = ~0;
      dzn_foreach_pool_type (type)
         binfos[i].range_idx[type] = ~0;
   }

   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      VkDescriptorType desc_type = ordered_bindings[i].descriptorType;
      uint32_t binding = ordered_bindings[i].binding;
      uint32_t desc_count = ordered_bindings[i].descriptorCount;
      bool has_sampler = dzn_desc_type_has_sampler(desc_type);
      bool has_immutable_samplers =
         has_sampler &&
         ordered_bindings[i].pImmutableSamplers != NULL;
      bool has_static_sampler = has_immutable_samplers && desc_count == 1;
      bool is_dynamic = is_dynamic_desc_type(desc_type);

      D3D12_SHADER_VISIBILITY visibility =
         translate_desc_visibility(ordered_bindings[i].stageFlags);
      binfos[binding].type = desc_type;
      binfos[binding].stages =
         translate_desc_stages(ordered_bindings[i].stageFlags);
      set_layout->stages |= binfos[binding].stages;
      binfos[binding].visibility = visibility;
      binfos[binding].base_shader_register = base_register;
      assert(base_register + desc_count >= base_register);
      base_register += desc_count;

      if (has_static_sampler) {
         VK_FROM_HANDLE(dzn_sampler, sampler, ordered_bindings[i].pImmutableSamplers[0]);

         /* Not all border colors are supported. */
         if (sampler->static_border_color != -1) {
            binfos[binding].static_sampler_idx = static_sampler_idx;
            D3D12_STATIC_SAMPLER_DESC *desc = (D3D12_STATIC_SAMPLER_DESC *)
               &static_samplers[static_sampler_idx];

            desc->Filter = sampler->desc.Filter;
            desc->AddressU = sampler->desc.AddressU;
            desc->AddressV = sampler->desc.AddressV;
            desc->AddressW = sampler->desc.AddressW;
            desc->MipLODBias = sampler->desc.MipLODBias;
            desc->MaxAnisotropy = sampler->desc.MaxAnisotropy;
            desc->ComparisonFunc = sampler->desc.ComparisonFunc;
            desc->BorderColor = sampler->static_border_color;
            desc->MinLOD = sampler->desc.MinLOD;
            desc->MaxLOD = sampler->desc.MaxLOD;
            desc->ShaderRegister = binfos[binding].base_shader_register;
            desc->ShaderVisibility = translate_desc_visibility(ordered_bindings[i].stageFlags);
            static_sampler_idx++;
         } else {
            has_static_sampler = false;
         }
      }

      if (has_immutable_samplers && !has_static_sampler) {
         binfos[binding].immutable_sampler_idx = immutable_sampler_idx;
         for (uint32_t s = 0; s < desc_count; s++) {
            VK_FROM_HANDLE(dzn_sampler, sampler, ordered_bindings[i].pImmutableSamplers[s]);

            immutable_samplers[immutable_sampler_idx++] = sampler;
         }
      }

      if (is_dynamic) {
         binfos[binding].dynamic_buffer_idx = dynamic_buffer_idx;
         for (uint32_t d = 0; d < desc_count; d++)
            set_layout->dynamic_buffers.bindings[dynamic_buffer_idx + d] = binding;
         dynamic_buffer_idx += desc_count;
         assert(dynamic_buffer_idx <= MAX_DYNAMIC_BUFFERS);
      }

      if (!ordered_bindings[i].descriptorCount)
         continue;

      unsigned num_descs =
         num_descs_for_type(desc_type, has_static_sampler);
      if (!num_descs) continue;

      assert(visibility < ARRAY_SIZE(set_layout->ranges));

      bool has_range[NUM_POOL_TYPES] = { 0 };
      has_range[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] =
         has_sampler && !has_static_sampler;
      has_range[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] =
         desc_type != VK_DESCRIPTOR_TYPE_SAMPLER;

      dzn_foreach_pool_type (type) {
         if (!has_range[type]) continue;

         uint32_t idx = range_idx[visibility][type]++;
         assert(idx < range_count[visibility][type]);

         binfos[binding].range_idx[type] = idx;
         D3D12_DESCRIPTOR_RANGE1 *range = (D3D12_DESCRIPTOR_RANGE1 *)
            &set_layout->ranges[visibility][type][idx];
         VkDescriptorType range_type = desc_type;
         if (desc_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            range_type = type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
                         VK_DESCRIPTOR_TYPE_SAMPLER :
                         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
         }
         range->RangeType = desc_type_to_range_type(range_type, false);
         range->NumDescriptors = desc_count;
         range->BaseShaderRegister = binfos[binding].base_shader_register;
         range->Flags = type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
            D3D12_DESCRIPTOR_RANGE_FLAG_NONE :
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS;
         if (is_dynamic) {
            range->OffsetInDescriptorsFromTableStart =
               set_layout->dynamic_buffers.range_offset +
               set_layout->dynamic_buffers.desc_count;
            set_layout->dynamic_buffers.count += range->NumDescriptors;
            set_layout->dynamic_buffers.desc_count += range->NumDescriptors;
         } else {
            range->OffsetInDescriptorsFromTableStart = set_layout->range_desc_count[type];
            set_layout->range_desc_count[type] += range->NumDescriptors;
         }

         if (!dzn_descriptor_type_depends_on_shader_usage(desc_type))
            continue;

         assert(idx + 1 < range_count[visibility][type]);
         range_idx[visibility][type]++;
         range[1] = range[0];
         range++;
         range->RangeType = desc_type_to_range_type(range_type, true);
         if (is_dynamic) {
            range->OffsetInDescriptorsFromTableStart =
               set_layout->dynamic_buffers.range_offset +
               set_layout->dynamic_buffers.desc_count;
            set_layout->dynamic_buffers.desc_count += range->NumDescriptors;
         } else {
            range->OffsetInDescriptorsFromTableStart = set_layout->range_desc_count[type];
            set_layout->range_desc_count[type] += range->NumDescriptors;
         }
      }
   }

   free(ordered_bindings);

   *out = dzn_descriptor_set_layout_to_handle(set_layout);
   return VK_SUCCESS;
}

static uint32_t
dzn_descriptor_set_layout_get_heap_offset(const struct dzn_descriptor_set_layout *layout,
                                          uint32_t b,
                                          D3D12_DESCRIPTOR_HEAP_TYPE type,
                                          bool writeable)
{
   assert(b < layout->binding_count);
   D3D12_SHADER_VISIBILITY visibility = layout->bindings[b].visibility;
   assert(visibility < ARRAY_SIZE(layout->ranges));
   assert(type < NUM_POOL_TYPES);

   uint32_t range_idx = layout->bindings[b].range_idx[type];

   if (range_idx == ~0)
      return ~0;

   if (writeable &&
       !dzn_descriptor_type_depends_on_shader_usage(layout->bindings[b].type))
      return ~0;

   if (writeable)
      range_idx++;

   assert(range_idx < layout->range_count[visibility][type]);
   return layout->ranges[visibility][type][range_idx].OffsetInDescriptorsFromTableStart;
}

static uint32_t
dzn_descriptor_set_layout_get_desc_count(const struct dzn_descriptor_set_layout *layout,
                                         uint32_t b)
{
   D3D12_SHADER_VISIBILITY visibility = layout->bindings[b].visibility;
   assert(visibility < ARRAY_SIZE(layout->ranges));

   dzn_foreach_pool_type (type) {
      uint32_t range_idx = layout->bindings[b].range_idx[type];
      assert(range_idx == ~0 || range_idx < layout->range_count[visibility][type]);

      if (range_idx != ~0)
         return layout->ranges[visibility][type][range_idx].NumDescriptors;
   }

   return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateDescriptorSetLayout(VkDevice device,
                              const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkDescriptorSetLayout *pSetLayout)
{
   return dzn_descriptor_set_layout_create(dzn_device_from_handle(device),
                                           pCreateInfo, pAllocator, pSetLayout);
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetDescriptorSetLayoutSupport(VkDevice device,
                                  const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                  VkDescriptorSetLayoutSupport *pSupport)
{
   const VkDescriptorSetLayoutBinding *bindings = pCreateInfo->pBindings;
   uint32_t sampler_count = 0, other_desc_count = 0;

   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      VkDescriptorType desc_type = bindings[i].descriptorType;
      bool has_sampler = dzn_desc_type_has_sampler(desc_type);

      if (has_sampler)
         sampler_count += bindings[i].descriptorCount;
      if (desc_type != VK_DESCRIPTOR_TYPE_SAMPLER)
         other_desc_count += bindings[i].descriptorCount;
      if (dzn_descriptor_type_depends_on_shader_usage(desc_type))
         other_desc_count += bindings[i].descriptorCount;
   }

   pSupport->supported =
      sampler_count <= (MAX_DESCS_PER_SAMPLER_HEAP / MAX_SETS) &&
      other_desc_count <= (MAX_DESCS_PER_CBV_SRV_UAV_HEAP / MAX_SETS);
}

static void
dzn_pipeline_layout_destroy(struct vk_device *vk_device,
                            struct vk_pipeline_layout *vk_layout)
{
   struct dzn_pipeline_layout *layout =
      container_of(vk_layout, struct dzn_pipeline_layout, vk);

   if (layout->root.sig)
      ID3D12RootSignature_Release(layout->root.sig);

   vk_pipeline_layout_destroy(vk_device, &layout->vk);
}

// Reserve two root parameters for the push constants and sysvals CBVs.
#define MAX_INTERNAL_ROOT_PARAMS 2

// One root parameter for samplers and the other one for views, multiplied by
// the number of visibility combinations, plus the internal root parameters.
#define MAX_ROOT_PARAMS ((MAX_SHADER_VISIBILITIES * 2) + MAX_INTERNAL_ROOT_PARAMS)

// Maximum number of DWORDS (32-bit words) that can be used for a root signature
#define MAX_ROOT_DWORDS 64

static void
dzn_pipeline_layout_hash_stages(struct dzn_pipeline_layout *layout,
                                const VkPipelineLayoutCreateInfo *info)
{
   uint32_t stages = 0;
   for (uint32_t stage = 0; stage < ARRAY_SIZE(layout->stages); stage++) {
      for (uint32_t set = 0; set < info->setLayoutCount; set++) {
         VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, info->pSetLayouts[set]);

         stages |= set_layout->stages;
      }
   }

   for (uint32_t stage = 0; stage < ARRAY_SIZE(layout->stages); stage++) {
      if (!(stages & BITFIELD_BIT(stage)))
         continue;

      struct mesa_sha1 ctx;

      _mesa_sha1_init(&ctx);
      for (uint32_t set = 0; set < info->setLayoutCount; set++) {
         VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, info->pSetLayouts[set]);
         if (!(BITFIELD_BIT(stage) & set_layout->stages))
            continue;

         for (uint32_t b = 0; b < set_layout->binding_count; b++) {
            if (!(BITFIELD_BIT(stage) & set_layout->bindings[b].stages))
               continue;

            _mesa_sha1_update(&ctx, &b, sizeof(b));
            _mesa_sha1_update(&ctx, &set_layout->bindings[b].base_shader_register,
                              sizeof(set_layout->bindings[b].base_shader_register));
         }
      }
      _mesa_sha1_final(&ctx, layout->stages[stage].hash);
   }
}

static VkResult
dzn_pipeline_layout_create(struct dzn_device *device,
                           const VkPipelineLayoutCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipelineLayout *out)
{
   uint32_t binding_count = 0;

   for (uint32_t s = 0; s < pCreateInfo->setLayoutCount; s++) {
      VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, pCreateInfo->pSetLayouts[s]);

      if (!set_layout)
         continue;

      binding_count += set_layout->binding_count;
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct dzn_pipeline_layout, layout, 1);
   VK_MULTIALLOC_DECL(&ma, uint32_t, binding_translation, binding_count);

   if (!vk_pipeline_layout_multizalloc(&device->vk, &ma, pCreateInfo))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   layout->vk.destroy = dzn_pipeline_layout_destroy;

   for (uint32_t s = 0; s < pCreateInfo->setLayoutCount; s++) {
      VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, pCreateInfo->pSetLayouts[s]);

      if (!set_layout || !set_layout->binding_count)
         continue;

      layout->binding_translation[s].base_reg = binding_translation;
      binding_translation += set_layout->binding_count;
   }

   uint32_t range_count = 0, static_sampler_count = 0;

   layout->root.param_count = 0;
   dzn_foreach_pool_type (type)
      layout->desc_count[type] = 0;

   layout->set_count = pCreateInfo->setLayoutCount;
   for (uint32_t j = 0; j < layout->set_count; j++) {
      VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, pCreateInfo->pSetLayouts[j]);
      uint32_t *binding_trans = layout->binding_translation[j].base_reg;

      layout->sets[j].dynamic_buffer_count = set_layout->dynamic_buffers.count;
      memcpy(layout->sets[j].range_desc_count, set_layout->range_desc_count,
             sizeof(layout->sets[j].range_desc_count));
      layout->binding_translation[j].binding_count = set_layout->binding_count;
      for (uint32_t b = 0; b < set_layout->binding_count; b++)
         binding_trans[b] = set_layout->bindings[b].base_shader_register;

      static_sampler_count += set_layout->static_sampler_count;
      dzn_foreach_pool_type (type) {
         layout->sets[j].heap_offsets[type] = layout->desc_count[type];
         layout->desc_count[type] += set_layout->range_desc_count[type];
         for (uint32_t i = 0; i < MAX_SHADER_VISIBILITIES; i++)
            range_count += set_layout->range_count[i][type];
      }

      layout->desc_count[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] +=
         set_layout->dynamic_buffers.desc_count;
      for (uint32_t o = 0, elem = 0; o < set_layout->dynamic_buffers.count; o++, elem++) {
         uint32_t b = set_layout->dynamic_buffers.bindings[o];

         if (o > 0 && set_layout->dynamic_buffers.bindings[o - 1] != b)
            elem = 0;

         uint32_t srv =
            dzn_descriptor_set_layout_get_heap_offset(set_layout, b, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);
         uint32_t uav =
            dzn_descriptor_set_layout_get_heap_offset(set_layout, b, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

         layout->sets[j].dynamic_buffer_heap_offsets[o].srv = srv != ~0 ? srv + elem : ~0;
         layout->sets[j].dynamic_buffer_heap_offsets[o].uav = uav != ~0 ? uav + elem : ~0;
      }
   }

   D3D12_DESCRIPTOR_RANGE1 *ranges =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*ranges) * range_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (range_count && !ranges) {
      vk_pipeline_layout_unref(&device->vk, &layout->vk);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   D3D12_STATIC_SAMPLER_DESC *static_sampler_descs =
      vk_alloc2(&device->vk.alloc, pAllocator,
                sizeof(*static_sampler_descs) * static_sampler_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (static_sampler_count && !static_sampler_descs) {
      vk_free2(&device->vk.alloc, pAllocator, ranges);
      vk_pipeline_layout_unref(&device->vk, &layout->vk);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }


   D3D12_ROOT_PARAMETER1 root_params[MAX_ROOT_PARAMS] = { 0 };
   D3D12_DESCRIPTOR_RANGE1 *range_ptr = ranges;
   D3D12_ROOT_PARAMETER1 *root_param;
   uint32_t root_dwords = 0;

   for (uint32_t i = 0; i < MAX_SHADER_VISIBILITIES; i++) {
      dzn_foreach_pool_type (type) {
         root_param = &root_params[layout->root.param_count];
         root_param->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
         root_param->DescriptorTable.pDescriptorRanges = range_ptr;
         root_param->DescriptorTable.NumDescriptorRanges = 0;
         root_param->ShaderVisibility = (D3D12_SHADER_VISIBILITY)i;

         for (uint32_t j = 0; j < pCreateInfo->setLayoutCount; j++) {
            VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, pCreateInfo->pSetLayouts[j]);
            uint32_t range_count = set_layout->range_count[i][type];

            memcpy(range_ptr, set_layout->ranges[i][type],
                   range_count * sizeof(D3D12_DESCRIPTOR_RANGE1));
            for (uint32_t k = 0; k < range_count; k++) {
               range_ptr[k].RegisterSpace = j;
               range_ptr[k].OffsetInDescriptorsFromTableStart +=
                  layout->sets[j].heap_offsets[type];
            }
            root_param->DescriptorTable.NumDescriptorRanges += range_count;
            range_ptr += range_count;
         }

         if (root_param->DescriptorTable.NumDescriptorRanges) {
            layout->root.type[layout->root.param_count++] = (D3D12_DESCRIPTOR_HEAP_TYPE)type;
            root_dwords++;
         }
      }
   }

   layout->root.sets_param_count = layout->root.param_count;

   /* Add our sysval CBV, and make it visible to all shaders */
   layout->root.sysval_cbv_param_idx = layout->root.param_count;
   root_param = &root_params[layout->root.param_count++];
   root_param->ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
   root_param->Descriptor.RegisterSpace = DZN_REGISTER_SPACE_SYSVALS;
   root_param->Constants.ShaderRegister = 0;
   root_param->Constants.Num32BitValues =
       DIV_ROUND_UP(MAX2(sizeof(struct dxil_spirv_vertex_runtime_data),
                         sizeof(struct dxil_spirv_compute_runtime_data)),
                    4);
   root_param->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   root_dwords += root_param->Constants.Num32BitValues;

   D3D12_STATIC_SAMPLER_DESC *static_sampler_ptr = static_sampler_descs;
   for (uint32_t j = 0; j < pCreateInfo->setLayoutCount; j++) {
      VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, pCreateInfo->pSetLayouts[j]);

      memcpy(static_sampler_ptr, set_layout->static_samplers,
             set_layout->static_sampler_count * sizeof(*set_layout->static_samplers));
      if (j > 0) {
         for (uint32_t k = 0; k < set_layout->static_sampler_count; k++)
            static_sampler_ptr[k].RegisterSpace = j;
      }
      static_sampler_ptr += set_layout->static_sampler_count;
   }

   uint32_t push_constant_size = 0;
   uint32_t push_constant_flags = 0;
   for (uint32_t j = 0; j < pCreateInfo->pushConstantRangeCount; j++) {
      const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + j;
      push_constant_size = MAX2(push_constant_size, range->offset + range->size);
      push_constant_flags |= range->stageFlags;
   }

   if (push_constant_size > 0) {
      layout->root.push_constant_cbv_param_idx = layout->root.param_count;
      D3D12_ROOT_PARAMETER1 *root_param = &root_params[layout->root.param_count++];

      root_param->ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      root_param->Constants.ShaderRegister = 0;
      root_param->Constants.Num32BitValues = ALIGN(push_constant_size, 4) / 4;
      root_param->Constants.RegisterSpace = DZN_REGISTER_SPACE_PUSH_CONSTANT;
      root_param->ShaderVisibility = translate_desc_visibility(push_constant_flags);
      root_dwords += root_param->Constants.Num32BitValues;
   }

   assert(layout->root.param_count <= ARRAY_SIZE(root_params));
   assert(root_dwords <= MAX_ROOT_DWORDS);

   D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {
      .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
      .Desc_1_1 = {
         .NumParameters = layout->root.param_count,
         .pParameters = layout->root.param_count ? root_params : NULL,
         .NumStaticSamplers =static_sampler_count,
         .pStaticSamplers = static_sampler_descs,
         /* TODO Only enable this flag when needed (optimization) */
         .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
      },
   };

   layout->root.sig = dzn_device_create_root_sig(device, &root_sig_desc);
   vk_free2(&device->vk.alloc, pAllocator, ranges);
   vk_free2(&device->vk.alloc, pAllocator, static_sampler_descs);

   if (!layout->root.sig) {
      vk_pipeline_layout_unref(&device->vk, &layout->vk);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   dzn_pipeline_layout_hash_stages(layout, pCreateInfo);
   *out = dzn_pipeline_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreatePipelineLayout(VkDevice device,
                         const VkPipelineLayoutCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkPipelineLayout *pPipelineLayout)
{
   return dzn_pipeline_layout_create(dzn_device_from_handle(device),
                                     pCreateInfo, pAllocator, pPipelineLayout);
}

static D3D12_DESCRIPTOR_HEAP_TYPE
desc_type_to_heap_type(VkDescriptorType in)
{
   switch (in) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
     return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
     return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
   default:
      unreachable("Unsupported desc type");
   }
}

static void
dzn_descriptor_heap_finish(struct dzn_descriptor_heap *heap)
{
   if (heap->heap)
      ID3D12DescriptorHeap_Release(heap->heap);

   if (heap->dev)
      ID3D12Device_Release(heap->dev);
}

static VkResult
dzn_descriptor_heap_init(struct dzn_descriptor_heap *heap,
                         struct dzn_device *device,
                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                         uint32_t desc_count,
                         bool shader_visible)
{
   heap->desc_count = desc_count;
   heap->type = type;
   heap->dev = device->dev;
   ID3D12Device1_AddRef(heap->dev);
   heap->desc_sz = ID3D12Device1_GetDescriptorHandleIncrementSize(device->dev, type);

   D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = type,
      .NumDescriptors = desc_count,
      .Flags = shader_visible ?
               D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE :
               D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
   };

   if (FAILED(ID3D12Device1_CreateDescriptorHeap(device->dev, &desc,
                                                 &IID_ID3D12DescriptorHeap,
                                                 (void **)&heap->heap))) {
      return vk_error(device,
                      shader_visible ?
                      VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = dzn_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap->heap);
   heap->cpu_base = cpu_handle.ptr;
   if (shader_visible) {
      D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = dzn_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap->heap);
      heap->gpu_base = gpu_handle.ptr;
   }

   return VK_SUCCESS;
}

D3D12_CPU_DESCRIPTOR_HANDLE
dzn_descriptor_heap_get_cpu_handle(const struct dzn_descriptor_heap *heap, uint32_t desc_offset)
{
   return (D3D12_CPU_DESCRIPTOR_HANDLE) {
      .ptr = heap->cpu_base + (desc_offset * heap->desc_sz),
   };
}

D3D12_GPU_DESCRIPTOR_HANDLE
dzn_descriptor_heap_get_gpu_handle(const struct dzn_descriptor_heap *heap, uint32_t desc_offset)
{
   return (D3D12_GPU_DESCRIPTOR_HANDLE) {
      .ptr = heap->gpu_base ? heap->gpu_base + (desc_offset * heap->desc_sz) : 0,
   };
}

static void
dzn_descriptor_heap_write_sampler_desc(struct dzn_descriptor_heap *heap,
                                       uint32_t desc_offset,
                                       const struct dzn_sampler *sampler)
{
   ID3D12Device1_CreateSampler(heap->dev, &sampler->desc,
                               dzn_descriptor_heap_get_cpu_handle(heap, desc_offset));
}

void
dzn_descriptor_heap_write_image_view_desc(struct dzn_descriptor_heap *heap,
                                          uint32_t desc_offset,
                                          bool writeable, bool cube_as_2darray,
                                          const struct dzn_image_view *iview)
{
   D3D12_CPU_DESCRIPTOR_HANDLE view_handle =
      dzn_descriptor_heap_get_cpu_handle(heap, desc_offset);
   struct dzn_image *image = container_of(iview->vk.image, struct dzn_image, vk);

   if (writeable) {
      ID3D12Device1_CreateUnorderedAccessView(heap->dev, image->res, NULL, &iview->uav_desc, view_handle);
   } else if (cube_as_2darray &&
              (iview->srv_desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY ||
               iview->srv_desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = iview->srv_desc;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      srv_desc.Texture2DArray.PlaneSlice = 0;
      if (iview->srv_desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY) {
         srv_desc.Texture2DArray.MostDetailedMip =
            iview->srv_desc.TextureCubeArray.MostDetailedMip;
         srv_desc.Texture2DArray.MipLevels =
            iview->srv_desc.TextureCubeArray.MipLevels;
         srv_desc.Texture2DArray.FirstArraySlice =
            iview->srv_desc.TextureCubeArray.First2DArrayFace;
         srv_desc.Texture2DArray.ArraySize =
            iview->srv_desc.TextureCubeArray.NumCubes * 6;
      } else {
         srv_desc.Texture2DArray.MostDetailedMip =
            iview->srv_desc.TextureCube.MostDetailedMip;
         srv_desc.Texture2DArray.MipLevels =
            iview->srv_desc.TextureCube.MipLevels;
         srv_desc.Texture2DArray.FirstArraySlice = 0;
         srv_desc.Texture2DArray.ArraySize = 6;
      }

      ID3D12Device1_CreateShaderResourceView(heap->dev, image->res, &srv_desc, view_handle);
   } else {
      ID3D12Device1_CreateShaderResourceView(heap->dev, image->res, &iview->srv_desc, view_handle);
   }
}

static void
dzn_descriptor_heap_write_buffer_view_desc(struct dzn_descriptor_heap *heap,
                                           uint32_t desc_offset,
                                           bool writeable,
                                           const struct dzn_buffer_view *bview)
{
   D3D12_CPU_DESCRIPTOR_HANDLE view_handle =
      dzn_descriptor_heap_get_cpu_handle(heap, desc_offset);

   if (writeable)
      ID3D12Device1_CreateUnorderedAccessView(heap->dev, bview->buffer->res, NULL, &bview->uav_desc, view_handle);
   else
      ID3D12Device1_CreateShaderResourceView(heap->dev, bview->buffer->res, &bview->srv_desc, view_handle);
}

void
dzn_descriptor_heap_write_buffer_desc(struct dzn_descriptor_heap *heap,
                                      uint32_t desc_offset,
                                      bool writeable,
                                      const struct dzn_buffer_desc *info)
{
   D3D12_CPU_DESCRIPTOR_HANDLE view_handle =
      dzn_descriptor_heap_get_cpu_handle(heap, desc_offset);

   VkDeviceSize size =
      info->range == VK_WHOLE_SIZE ?
      info->buffer->size - info->offset :
      info->range;

   if (info->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
       info->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
      assert(!writeable);
      D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
         .BufferLocation = ID3D12Resource_GetGPUVirtualAddress(info->buffer->res) + info->offset,
         .SizeInBytes = ALIGN_POT(size, 256),
      };
      ID3D12Device1_CreateConstantBufferView(heap->dev, &cbv_desc, view_handle);
   } else if (writeable) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
         .Format = DXGI_FORMAT_R32_TYPELESS,
         .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
         .Buffer = {
            .FirstElement = info->offset / sizeof(uint32_t),
            .NumElements = (UINT)size / sizeof(uint32_t),
            .Flags = D3D12_BUFFER_UAV_FLAG_RAW,
         },
      };
      ID3D12Device1_CreateUnorderedAccessView(heap->dev, info->buffer->res, NULL, &uav_desc, view_handle);
   } else {
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
         .Format = DXGI_FORMAT_R32_TYPELESS,
         .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
         .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
         .Buffer = {
            .FirstElement = info->offset / sizeof(uint32_t),
            .NumElements = (UINT)size / sizeof(uint32_t),
            .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
         },
      };
      ID3D12Device1_CreateShaderResourceView(heap->dev, info->buffer->res, &srv_desc, view_handle);
   }
}

void
dzn_descriptor_heap_copy(struct dzn_descriptor_heap *dst_heap,
                         uint32_t dst_offset,
                         const struct dzn_descriptor_heap *src_heap,
                         uint32_t src_offset,
                         uint32_t desc_count)
{
   D3D12_CPU_DESCRIPTOR_HANDLE dst_handle =
      dzn_descriptor_heap_get_cpu_handle(dst_heap, dst_offset);
   D3D12_CPU_DESCRIPTOR_HANDLE src_handle =
      dzn_descriptor_heap_get_cpu_handle(src_heap, src_offset);

   ID3D12Device1_CopyDescriptorsSimple(dst_heap->dev, desc_count,
                                       dst_handle,
                                       src_handle,
                                       dst_heap->type);
}

struct dzn_descriptor_set_ptr {
   uint32_t binding, elem;
};

static void
dzn_descriptor_set_ptr_validate(const struct dzn_descriptor_set_layout *layout,
                                struct dzn_descriptor_set_ptr *ptr)
{

   if (ptr->binding >= layout->binding_count) {
      ptr->binding = ~0;
      ptr->elem = ~0;
      return;
   }

   uint32_t desc_count =
      dzn_descriptor_set_layout_get_desc_count(layout, ptr->binding);
   if (ptr->elem >= desc_count) {
      ptr->binding = ~0;
      ptr->elem = ~0;
   }
}

static void
dzn_descriptor_set_ptr_init(const struct dzn_descriptor_set_layout *layout,
                            struct dzn_descriptor_set_ptr *ptr,
                            uint32_t binding, uint32_t elem)
{
   ptr->binding = binding;
   ptr->elem = elem;
   dzn_descriptor_set_ptr_validate(layout, ptr);
}

static void
dzn_descriptor_set_ptr_move(const struct dzn_descriptor_set_layout *layout,
                            struct dzn_descriptor_set_ptr *ptr,
                            uint32_t count)
{
   if (ptr->binding == ~0)
      return;

   while (count) {
      uint32_t desc_count =
         dzn_descriptor_set_layout_get_desc_count(layout, ptr->binding);

      if (count >= desc_count - ptr->elem) {
         count -= desc_count - ptr->elem;
         ptr->binding++;
         ptr->elem = 0;
      } else {
         ptr->elem += count;
         count = 0;
      }
   }

   dzn_descriptor_set_ptr_validate(layout, ptr);
}

static bool
dzn_descriptor_set_ptr_is_valid(const struct dzn_descriptor_set_ptr *ptr)
{
   return ptr->binding != ~0 && ptr->elem != ~0;
}

static uint32_t
dzn_descriptor_set_remaining_descs_in_binding(const struct dzn_descriptor_set_layout *layout,
                                              const struct dzn_descriptor_set_ptr *ptr)
{
   if (ptr->binding >= layout->binding_count)
      return 0;

   uint32_t desc_count =
      dzn_descriptor_set_layout_get_desc_count(layout, ptr->binding);

   return desc_count >= ptr->elem ? desc_count - ptr->elem : 0;
}


static uint32_t
dzn_descriptor_set_ptr_get_heap_offset(const struct dzn_descriptor_set_layout *layout,
                                       D3D12_DESCRIPTOR_HEAP_TYPE type,
                                       const struct dzn_descriptor_set_ptr *ptr,
                                       bool writeable)
{
   if (ptr->binding == ~0)
      return ~0;

   uint32_t base =
      dzn_descriptor_set_layout_get_heap_offset(layout, ptr->binding, type, writeable);
   if (base == ~0)
      return ~0;

   return base + ptr->elem;
}

static void
dzn_descriptor_set_write_sampler_desc(struct dzn_descriptor_set *set,
                                      uint32_t heap_offset,
                                      const struct dzn_sampler *sampler)
{
   if (heap_offset == ~0)
      return;

   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

   mtx_lock(&set->pool->defragment_lock);
   dzn_descriptor_heap_write_sampler_desc(&set->pool->heaps[type],
                                          set->heap_offsets[type] + heap_offset,
                                          sampler);
    mtx_unlock(&set->pool->defragment_lock);
}

static void
dzn_descriptor_set_ptr_write_sampler_desc(struct dzn_descriptor_set *set,
                                          const struct dzn_descriptor_set_ptr *ptr,
                                          const struct dzn_sampler *sampler)
{
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
   uint32_t heap_offset =
      dzn_descriptor_set_ptr_get_heap_offset(set->layout, type, ptr, false);

   dzn_descriptor_set_write_sampler_desc(set, heap_offset, sampler);
}

static uint32_t
dzn_descriptor_set_ptr_get_dynamic_buffer_idx(const struct dzn_descriptor_set_layout *layout,
                                              const struct dzn_descriptor_set_ptr *ptr)
{
   if (ptr->binding == ~0)
      return ~0;

   uint32_t base = layout->bindings[ptr->binding].dynamic_buffer_idx;

   if (base == ~0)
      return ~0;

   return base + ptr->elem;
}

static void
dzn_descriptor_set_write_dynamic_buffer_desc(struct dzn_descriptor_set *set,
                                             uint32_t dynamic_buffer_idx,
                                             const struct dzn_buffer_desc *info)
{
   if (dynamic_buffer_idx == ~0)
      return;

   assert(dynamic_buffer_idx < set->layout->dynamic_buffers.count);
   set->dynamic_buffers[dynamic_buffer_idx] = *info;
}

static void
dzn_descriptor_set_ptr_write_dynamic_buffer_desc(struct dzn_descriptor_set *set,
                                                 const struct dzn_descriptor_set_ptr *ptr,
                                                 const struct dzn_buffer_desc *info)
{
   uint32_t dynamic_buffer_idx =
      dzn_descriptor_set_ptr_get_dynamic_buffer_idx(set->layout, ptr);

   dzn_descriptor_set_write_dynamic_buffer_desc(set, dynamic_buffer_idx, info);
}

static VkDescriptorType
dzn_descriptor_set_ptr_get_vk_type(const struct dzn_descriptor_set_layout *layout,
                                   const struct dzn_descriptor_set_ptr *ptr)
{
   if (ptr->binding >= layout->binding_count)
      return (VkDescriptorType)~0;

   return layout->bindings[ptr->binding].type;
}

static void
dzn_descriptor_set_write_image_view_desc(struct dzn_descriptor_set *set,
                                         uint32_t heap_offset,
                                         uint32_t alt_heap_offset,
                                         bool cube_as_2darray,
                                         const struct dzn_image_view *iview)
{
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

   if (heap_offset == ~0)
      return;

   mtx_lock(&set->pool->defragment_lock);
   dzn_descriptor_heap_write_image_view_desc(&set->pool->heaps[type],
                                             set->heap_offsets[type] + heap_offset,
                                             false, cube_as_2darray,
                                             iview);

   if (alt_heap_offset != ~0) {
      dzn_descriptor_heap_write_image_view_desc(&set->pool->heaps[type],
                                                set->heap_offsets[type] + alt_heap_offset,
                                                true, cube_as_2darray,
                                                iview);
   }
   mtx_unlock(&set->pool->defragment_lock);
}

static void
dzn_descriptor_set_ptr_write_image_view_desc(struct dzn_descriptor_set *set,
                                             const struct dzn_descriptor_set_ptr *ptr,
                                             bool cube_as_2darray,
                                             const struct dzn_image_view *iview)
{
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   uint32_t heap_offset =
      dzn_descriptor_set_ptr_get_heap_offset(set->layout, type, ptr, false);
   uint32_t alt_heap_offset =
      dzn_descriptor_set_ptr_get_heap_offset(set->layout, type, ptr, true);

   dzn_descriptor_set_write_image_view_desc(set, heap_offset, alt_heap_offset,
                                            cube_as_2darray, iview);
}

static void
dzn_descriptor_set_write_buffer_view_desc(struct dzn_descriptor_set *set,
                                          uint32_t heap_offset,
                                          uint32_t alt_heap_offset,
                                          const struct dzn_buffer_view *bview)
{
   if (heap_offset == ~0)
      return;

   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

   mtx_lock(&set->pool->defragment_lock);
   dzn_descriptor_heap_write_buffer_view_desc(&set->pool->heaps[type],
                                              set->heap_offsets[type] +
                                              heap_offset,
                                              false, bview);

   if (alt_heap_offset != ~0) {
      dzn_descriptor_heap_write_buffer_view_desc(&set->pool->heaps[type],
                                                 set->heap_offsets[type] +
                                                 alt_heap_offset,
                                                 true, bview);
   }
   mtx_unlock(&set->pool->defragment_lock);
}

static void
dzn_descriptor_set_ptr_write_buffer_view_desc(struct dzn_descriptor_set *set,
                                              const struct dzn_descriptor_set_ptr *ptr,
                                              const struct dzn_buffer_view *bview)
{
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   uint32_t heap_offset =
      dzn_descriptor_set_ptr_get_heap_offset(set->layout, type, ptr, false);
   uint32_t alt_heap_offset =
      dzn_descriptor_set_ptr_get_heap_offset(set->layout, type, ptr, true);

   dzn_descriptor_set_write_buffer_view_desc(set, heap_offset, alt_heap_offset, bview);
}

static void
dzn_descriptor_set_write_buffer_desc(struct dzn_descriptor_set *set,
                                     uint32_t heap_offset,
                                     uint32_t alt_heap_offset,
                                     const struct dzn_buffer_desc *bdesc)
{
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   if (heap_offset == ~0)
      return;

   mtx_lock(&set->pool->defragment_lock);
   dzn_descriptor_heap_write_buffer_desc(&set->pool->heaps[type],
                                         set->heap_offsets[type] + heap_offset,
                                         false, bdesc);

   if (alt_heap_offset != ~0) {
      dzn_descriptor_heap_write_buffer_desc(&set->pool->heaps[type],
                                            set->heap_offsets[type] +
                                            alt_heap_offset,
                                            true, bdesc);
   }
   mtx_unlock(&set->pool->defragment_lock);
}

static void
dzn_descriptor_set_ptr_write_buffer_desc(struct dzn_descriptor_set *set,
                                         const struct dzn_descriptor_set_ptr *ptr,
                                         const struct dzn_buffer_desc *bdesc)
{
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   uint32_t heap_offset =
      dzn_descriptor_set_ptr_get_heap_offset(set->layout, type, ptr, false);
   uint32_t alt_heap_offset =
      dzn_descriptor_set_ptr_get_heap_offset(set->layout, type, ptr, true);

   dzn_descriptor_set_write_buffer_desc(set, heap_offset, alt_heap_offset, bdesc);
}

static void
dzn_descriptor_set_init(struct dzn_descriptor_set *set,
                        struct dzn_device *device,
                        struct dzn_descriptor_pool *pool,
                        struct dzn_descriptor_set_layout *layout)
{
   vk_object_base_init(&device->vk, &set->base, VK_OBJECT_TYPE_DESCRIPTOR_SET);

   set->pool = pool;
   set->layout = layout;

   mtx_lock(&pool->defragment_lock);
   dzn_foreach_pool_type(type) {
      set->heap_offsets[type] = pool->free_offset[type];
      set->heap_sizes[type] = layout->range_desc_count[type];
      set->pool->free_offset[type] += layout->range_desc_count[type];
   }
   mtx_unlock(&pool->defragment_lock);

   /* Pre-fill the immutable samplers */
   if (layout->immutable_sampler_count) {
      for (uint32_t b = 0; b < layout->binding_count; b++) {
         bool has_samplers =
            dzn_desc_type_has_sampler(layout->bindings[b].type);

         if (!has_samplers || layout->bindings[b].immutable_sampler_idx == ~0)
            continue;

         struct dzn_descriptor_set_ptr ptr;
         const struct dzn_sampler **sampler =
            &layout->immutable_samplers[layout->bindings[b].immutable_sampler_idx];
         for (dzn_descriptor_set_ptr_init(set->layout, &ptr, b, 0);
              dzn_descriptor_set_ptr_is_valid(&ptr);
              dzn_descriptor_set_ptr_move(set->layout, &ptr, 1)) {
            dzn_descriptor_set_ptr_write_sampler_desc(set, &ptr, *sampler);
            sampler++;
         }
      }
   }
}

static void
dzn_descriptor_set_finish(struct dzn_descriptor_set *set)
{
   vk_object_base_finish(&set->base);
   set->pool = NULL;
   set->layout = NULL;
}

static void
dzn_descriptor_pool_destroy(struct dzn_descriptor_pool *pool,
                            const VkAllocationCallbacks *pAllocator)
{
   if (!pool)
      return;

   struct dzn_device *device = container_of(pool->base.device, struct dzn_device, vk);

   dzn_foreach_pool_type (type) {
      if (pool->desc_count[type])
         dzn_descriptor_heap_finish(&pool->heaps[type]);
   }

   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

static VkResult
dzn_descriptor_pool_create(struct dzn_device *device,
                           const VkDescriptorPoolCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkDescriptorPool *out)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct dzn_descriptor_pool, pool, 1);
   VK_MULTIALLOC_DECL(&ma, struct dzn_descriptor_set, sets, pCreateInfo->maxSets);

   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pool->alloc = pAllocator ? *pAllocator : device->vk.alloc;
   pool->sets = sets;
   pool->set_count = pCreateInfo->maxSets;
   mtx_init(&pool->defragment_lock, mtx_plain);

   vk_object_base_init(&device->vk, &pool->base, VK_OBJECT_TYPE_DESCRIPTOR_POOL);

   for (uint32_t p = 0; p < pCreateInfo->poolSizeCount; p++) {
      VkDescriptorType type = pCreateInfo->pPoolSizes[p].type;
      uint32_t num_desc = pCreateInfo->pPoolSizes[p].descriptorCount;

      switch (type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         pool->desc_count[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] += num_desc;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         pool->desc_count[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] += num_desc;
         pool->desc_count[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] += num_desc;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         pool->desc_count[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] += num_desc;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         /* Reserve one UAV and one SRV slot for those. */
         pool->desc_count[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] += num_desc * 2;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         break;
      default:
         unreachable("Unsupported desc type");
      }
   }

   dzn_foreach_pool_type (type) {
      if (!pool->desc_count[type])
         continue;

      VkResult result =
         dzn_descriptor_heap_init(&pool->heaps[type], device, type, pool->desc_count[type], false);
      if (result != VK_SUCCESS) {
         dzn_descriptor_pool_destroy(pool, pAllocator);
         return result;
      }
   }

   *out = dzn_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

static VkResult
dzn_descriptor_pool_defragment_heap(struct dzn_descriptor_pool *pool,
                                    D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   struct dzn_device *device = container_of(pool->base.device, struct dzn_device, vk);
   struct dzn_descriptor_heap new_heap;

   VkResult result =
      dzn_descriptor_heap_init(&new_heap, device, type,
                               pool->heaps[type].desc_count,
                               false);
   if (result != VK_SUCCESS)
      return result;

   mtx_lock(&pool->defragment_lock);
   uint32_t heap_offset = 0;
   for (uint32_t s = 0; s < pool->set_count; s++) {
      if (!pool->sets[s].layout)
         continue;

      dzn_descriptor_heap_copy(&new_heap, heap_offset,
                               &pool->heaps[type],
                               pool->sets[s].heap_offsets[type],
                               pool->sets[s].heap_sizes[type]);
      pool->sets[s].heap_offsets[type] = heap_offset;
      heap_offset += pool->sets[s].heap_sizes[type];
   }
   mtx_unlock(&pool->defragment_lock);

   dzn_descriptor_heap_finish(&pool->heaps[type]);
   pool->heaps[type] = new_heap;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateDescriptorPool(VkDevice device,
                         const VkDescriptorPoolCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkDescriptorPool *pDescriptorPool)
{
   return dzn_descriptor_pool_create(dzn_device_from_handle(device),
                                     pCreateInfo, pAllocator, pDescriptorPool);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyDescriptorPool(VkDevice device,
                          VkDescriptorPool descriptorPool,
                          const VkAllocationCallbacks *pAllocator)
{
   dzn_descriptor_pool_destroy(dzn_descriptor_pool_from_handle(descriptorPool),
                               pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_ResetDescriptorPool(VkDevice device,
                        VkDescriptorPool descriptorPool,
                        VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(dzn_descriptor_pool, pool, descriptorPool);

   for (uint32_t s = 0; s < pool->set_count; s++)
      dzn_descriptor_set_finish(&pool->sets[s]);

   dzn_foreach_pool_type(type)
      pool->free_offset[type] = 0;

   return VK_SUCCESS;
}

void
dzn_descriptor_heap_pool_finish(struct dzn_descriptor_heap_pool *pool)
{
   list_splicetail(&pool->active_heaps, &pool->free_heaps);
   list_for_each_entry_safe(struct dzn_descriptor_heap_pool_entry, entry, &pool->free_heaps, link) {
      list_del(&entry->link);
      dzn_descriptor_heap_finish(&entry->heap);
      vk_free(pool->alloc, entry);
   }
}

void
dzn_descriptor_heap_pool_init(struct dzn_descriptor_heap_pool *pool,
                              struct dzn_device *device,
                              D3D12_DESCRIPTOR_HEAP_TYPE type,
                              bool shader_visible,
                              const VkAllocationCallbacks *alloc)
{
   assert(!shader_visible ||
          type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
          type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

   pool->alloc = alloc;
   pool->type = type;
   pool->shader_visible = shader_visible;
   list_inithead(&pool->active_heaps);
   list_inithead(&pool->free_heaps);
   pool->offset = 0;
   pool->desc_sz = ID3D12Device1_GetDescriptorHandleIncrementSize(device->dev, type);
}

VkResult
dzn_descriptor_heap_pool_alloc_slots(struct dzn_descriptor_heap_pool *pool,
                                     struct dzn_device *device, uint32_t desc_count,
                                     struct dzn_descriptor_heap **heap,
                                     uint32_t *first_slot)
{
   struct dzn_descriptor_heap *last_heap =
      list_is_empty(&pool->active_heaps) ?
      NULL :
      &(list_last_entry(&pool->active_heaps, struct dzn_descriptor_heap_pool_entry, link)->heap);
   uint32_t last_heap_desc_count =
      last_heap ? last_heap->desc_count : 0;

   if (pool->offset + desc_count > last_heap_desc_count) {
      uint32_t granularity =
         (pool->type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
          pool->type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) ?
         64 * 1024 : 4 * 1024;
      uint32_t alloc_step = ALIGN_POT(desc_count * pool->desc_sz, granularity);
      uint32_t heap_desc_count = MAX2(alloc_step / pool->desc_sz, 16);

      /* Maximum of 2048 samplers per heap when shader_visible is true. */
      if (pool->shader_visible &&
          pool->type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
         assert(desc_count <= MAX_DESCS_PER_SAMPLER_HEAP);
         heap_desc_count = MIN2(heap_desc_count, MAX_DESCS_PER_SAMPLER_HEAP);
      }

      struct dzn_descriptor_heap_pool_entry *new_heap = NULL;

      list_for_each_entry_safe(struct dzn_descriptor_heap_pool_entry, entry, &pool->free_heaps, link) {
         if (entry->heap.desc_count >= heap_desc_count) {
            new_heap = entry;
            list_del(&entry->link);
            break;
         }
      }

      if (!new_heap) {
         new_heap =
            vk_zalloc(pool->alloc, sizeof(*new_heap), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (!new_heap)
            return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

         VkResult result =
            dzn_descriptor_heap_init(&new_heap->heap, device, pool->type,
                                     heap_desc_count, pool->shader_visible);
         if (result != VK_SUCCESS) {
            vk_free(&device->vk.alloc, new_heap);
            return result;
         }
      }

      list_addtail(&new_heap->link, &pool->active_heaps);
      pool->offset = 0;
      last_heap = &new_heap->heap;
   }

   *heap = last_heap;
   *first_slot = pool->offset;
   pool->offset += desc_count;
   return VK_SUCCESS;
}

void
dzn_descriptor_heap_pool_reset(struct dzn_descriptor_heap_pool *pool)
{
   pool->offset = 0;
   list_splicetail(&pool->active_heaps, &pool->free_heaps);
   list_inithead(&pool->active_heaps);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_AllocateDescriptorSets(VkDevice dev,
                           const VkDescriptorSetAllocateInfo *pAllocateInfo,
                           VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(dzn_descriptor_pool, pool, pAllocateInfo->descriptorPool);
   VK_FROM_HANDLE(dzn_device, device, dev);
   VkResult result;
   unsigned i;

   if (pAllocateInfo->descriptorSetCount > (pool->set_count - pool->used_set_count))
      return VK_ERROR_OUT_OF_POOL_MEMORY;

   uint32_t set_idx = 0;
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      VK_FROM_HANDLE(dzn_descriptor_set_layout, layout, pAllocateInfo->pSetLayouts[i]);

      dzn_foreach_pool_type(type) {
         if (pool->used_desc_count[type] + layout->range_desc_count[type] > pool->desc_count[type]) {
            dzn_FreeDescriptorSets(dev, pAllocateInfo->descriptorPool, i, pDescriptorSets);
            return vk_error(device, VK_ERROR_OUT_OF_POOL_MEMORY);
         }

         if (pool->free_offset[type] + layout->range_desc_count[type] > pool->desc_count[type]) {
            result = dzn_descriptor_pool_defragment_heap(pool, type);
            if (result != VK_SUCCESS) {
               dzn_FreeDescriptorSets(dev, pAllocateInfo->descriptorPool, i, pDescriptorSets);
               return vk_error(device, VK_ERROR_FRAGMENTED_POOL);
            }
         }
      }

      struct dzn_descriptor_set *set = NULL;
      for (; set_idx < pool->set_count; set_idx++) {
         if (!pool->sets[set_idx].layout) {
            set = &pool->sets[set_idx];
            break;
         }
      }

      dzn_descriptor_set_init(set, device, pool, layout);
      pDescriptorSets[i] = dzn_descriptor_set_to_handle(set);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_FreeDescriptorSets(VkDevice dev,
                       VkDescriptorPool descriptorPool,
                       uint32_t count,
                       const VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(dzn_descriptor_pool, pool, descriptorPool);

   for (uint32_t s = 0; s < count; s++) {
      VK_FROM_HANDLE(dzn_descriptor_set, set, pDescriptorSets[s]);

      if (!set)
         continue;

      assert(set->pool == pool);

      dzn_descriptor_set_finish(set);
   }

   mtx_lock(&pool->defragment_lock);
   dzn_foreach_pool_type(type)
      pool->free_offset[type] = 0;

   for (uint32_t s = 0; s < pool->set_count; s++) {
      const struct dzn_descriptor_set *set = &pool->sets[s];

      if (set->layout) {
         dzn_foreach_pool_type (type) {
            pool->free_offset[type] =
               MAX2(pool->free_offset[type],
                    set->heap_offsets[type] +
                    set->layout->range_desc_count[type]);
         }
      }
   }
   mtx_unlock(&pool->defragment_lock);

   return VK_SUCCESS;
}

static void
dzn_descriptor_set_write(const VkWriteDescriptorSet *pDescriptorWrite)
{
   VK_FROM_HANDLE(dzn_descriptor_set, set, pDescriptorWrite->dstSet);

   struct dzn_descriptor_set_ptr ptr;

   dzn_descriptor_set_ptr_init(set->layout, &ptr,
                               pDescriptorWrite->dstBinding,
                               pDescriptorWrite->dstArrayElement);
   uint32_t desc_count = pDescriptorWrite->descriptorCount;

   uint32_t d = 0;
   bool cube_as_2darray =
      pDescriptorWrite->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

   switch (pDescriptorWrite->descriptorType) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      for (; dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count;
           dzn_descriptor_set_ptr_move(set->layout, &ptr, 1)) {
         assert(dzn_descriptor_set_ptr_get_vk_type(set->layout, &ptr) == pDescriptorWrite->descriptorType);
         const VkDescriptorImageInfo *pImageInfo = pDescriptorWrite->pImageInfo + d;
         VK_FROM_HANDLE(dzn_sampler, sampler, pImageInfo->sampler);

         if (sampler)
            dzn_descriptor_set_ptr_write_sampler_desc(set, &ptr, sampler);

         d++;
      }
      break;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      for (; dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count;
           dzn_descriptor_set_ptr_move(set->layout, &ptr, 1)) {
         assert(dzn_descriptor_set_ptr_get_vk_type(set->layout, &ptr) == pDescriptorWrite->descriptorType);
         const VkDescriptorImageInfo *pImageInfo = pDescriptorWrite->pImageInfo + d;
         VK_FROM_HANDLE(dzn_sampler, sampler, pImageInfo->sampler);
         VK_FROM_HANDLE(dzn_image_view, iview, pImageInfo->imageView);

         if (sampler)
            dzn_descriptor_set_ptr_write_sampler_desc(set, &ptr, sampler);

         if (iview)
            dzn_descriptor_set_ptr_write_image_view_desc(set, &ptr, cube_as_2darray, iview);

         d++;
      }
      break;

   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      for (; dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count;
           dzn_descriptor_set_ptr_move(set->layout, &ptr, 1)) {
         assert(dzn_descriptor_set_ptr_get_vk_type(set->layout, &ptr) == pDescriptorWrite->descriptorType);
         const VkDescriptorImageInfo *pImageInfo = pDescriptorWrite->pImageInfo + d;
         VK_FROM_HANDLE(dzn_image_view, iview, pImageInfo->imageView);

         if (iview)
            dzn_descriptor_set_ptr_write_image_view_desc(set, &ptr, cube_as_2darray, iview);

         d++;
      }
      break;
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      for (; dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count;
           dzn_descriptor_set_ptr_move(set->layout, &ptr, 1)) {
         assert(dzn_descriptor_set_ptr_get_vk_type(set->layout, &ptr) == pDescriptorWrite->descriptorType);
         const VkDescriptorBufferInfo *binfo = &pDescriptorWrite->pBufferInfo[d];
         struct dzn_buffer_desc desc = {
            pDescriptorWrite->descriptorType,
            dzn_buffer_from_handle(binfo->buffer),
            binfo->range, binfo->offset
         };

         if (desc.buffer)
            dzn_descriptor_set_ptr_write_buffer_desc(set, &ptr, &desc);

         d++;
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      for (; dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count;
           dzn_descriptor_set_ptr_move(set->layout, &ptr, 1)) {
         assert(dzn_descriptor_set_ptr_get_vk_type(set->layout, &ptr) == pDescriptorWrite->descriptorType);
         const VkDescriptorBufferInfo *binfo = &pDescriptorWrite->pBufferInfo[d];
         struct dzn_buffer_desc desc = {
            pDescriptorWrite->descriptorType,
            dzn_buffer_from_handle(binfo->buffer),
            binfo->range, binfo->offset
         };

         if (desc.buffer)
            dzn_descriptor_set_ptr_write_dynamic_buffer_desc(set, &ptr, &desc);

         d++;
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      for (; dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count;
           dzn_descriptor_set_ptr_move(set->layout, &ptr, 1)) {
         assert(dzn_descriptor_set_ptr_get_vk_type(set->layout, &ptr) == pDescriptorWrite->descriptorType);
         VK_FROM_HANDLE(dzn_buffer_view, bview, pDescriptorWrite->pTexelBufferView[d]);

         if (bview)
            dzn_descriptor_set_ptr_write_buffer_view_desc(set, &ptr, bview);

         d++;
      }
      break;

   default:
      unreachable("invalid descriptor type");
      break;
   }

   assert(d == pDescriptorWrite->descriptorCount);
}

static void
dzn_descriptor_set_copy(const VkCopyDescriptorSet *pDescriptorCopy)
{
   VK_FROM_HANDLE(dzn_descriptor_set, src_set, pDescriptorCopy->srcSet);
   VK_FROM_HANDLE(dzn_descriptor_set, dst_set, pDescriptorCopy->dstSet);
   struct dzn_descriptor_set_ptr src_ptr, dst_ptr;

   dzn_descriptor_set_ptr_init(src_set->layout, &src_ptr,
                               pDescriptorCopy->srcBinding,
                               pDescriptorCopy->srcArrayElement);
   dzn_descriptor_set_ptr_init(dst_set->layout, &dst_ptr,
                               pDescriptorCopy->dstBinding,
                               pDescriptorCopy->dstArrayElement);

   uint32_t copied_count = 0;

   while (dzn_descriptor_set_ptr_is_valid(&src_ptr) &&
          dzn_descriptor_set_ptr_is_valid(&dst_ptr) &&
          copied_count < pDescriptorCopy->descriptorCount) {
      VkDescriptorType src_type =
         dzn_descriptor_set_ptr_get_vk_type(src_set->layout, &src_ptr);
      ASSERTED VkDescriptorType dst_type =
         dzn_descriptor_set_ptr_get_vk_type(dst_set->layout, &dst_ptr);

      assert(src_type == dst_type);
      uint32_t count =
         MIN2(dzn_descriptor_set_remaining_descs_in_binding(src_set->layout, &src_ptr),
              dzn_descriptor_set_remaining_descs_in_binding(dst_set->layout, &dst_ptr));

      if (src_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
          src_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
         uint32_t src_idx =
            dzn_descriptor_set_ptr_get_dynamic_buffer_idx(src_set->layout, &src_ptr);
         uint32_t dst_idx =
            dzn_descriptor_set_ptr_get_dynamic_buffer_idx(dst_set->layout, &dst_ptr);

         memcpy(&dst_set->dynamic_buffers[dst_idx],
                &src_set->dynamic_buffers[src_idx],
                sizeof(*dst_set->dynamic_buffers) * count);
      } else {
         dzn_foreach_pool_type(type) {
            uint32_t src_heap_offset =
               dzn_descriptor_set_ptr_get_heap_offset(src_set->layout, type, &src_ptr, false);
            uint32_t dst_heap_offset =
               dzn_descriptor_set_ptr_get_heap_offset(dst_set->layout, type, &dst_ptr, false);

            if (src_heap_offset == ~0) {
               assert(dst_heap_offset == ~0);
               continue;
            }

            mtx_lock(&src_set->pool->defragment_lock);
            mtx_lock(&dst_set->pool->defragment_lock);
            dzn_descriptor_heap_copy(&dst_set->pool->heaps[type],
                                     dst_set->heap_offsets[type] + dst_heap_offset,
                                     &src_set->pool->heaps[type],
                                     src_set->heap_offsets[type] + src_heap_offset,
                                     count);

            if (dzn_descriptor_type_depends_on_shader_usage(src_type)) {
               src_heap_offset =
                  dzn_descriptor_set_ptr_get_heap_offset(src_set->layout, type, &src_ptr, true);
               dst_heap_offset =
                  dzn_descriptor_set_ptr_get_heap_offset(dst_set->layout, type, &dst_ptr, true);
               assert(src_heap_offset != ~0);
               assert(dst_heap_offset != ~0);
               dzn_descriptor_heap_copy(&dst_set->pool->heaps[type],
                                        dst_set->heap_offsets[type] + dst_heap_offset,
                                        &src_set->pool->heaps[type],
                                        src_set->heap_offsets[type] + src_heap_offset,
                                        count);
            }
            mtx_unlock(&dst_set->pool->defragment_lock);
            mtx_unlock(&src_set->pool->defragment_lock);
         }
      }

      dzn_descriptor_set_ptr_move(src_set->layout, &src_ptr, count);
      dzn_descriptor_set_ptr_move(dst_set->layout, &dst_ptr, count);
      copied_count += count;
   }

   assert(copied_count == pDescriptorCopy->descriptorCount);
}

VKAPI_ATTR void VKAPI_CALL
dzn_UpdateDescriptorSets(VkDevice _device,
                         uint32_t descriptorWriteCount,
                         const VkWriteDescriptorSet *pDescriptorWrites,
                         uint32_t descriptorCopyCount,
                         const VkCopyDescriptorSet *pDescriptorCopies)
{
   for (unsigned i = 0; i < descriptorWriteCount; i++)
      dzn_descriptor_set_write(&pDescriptorWrites[i]);

   for (unsigned i = 0; i < descriptorCopyCount; i++)
      dzn_descriptor_set_copy(&pDescriptorCopies[i]);
}

static void
dzn_descriptor_update_template_destroy(struct dzn_descriptor_update_template *templ,
                                       const VkAllocationCallbacks *alloc)
{
   if (!templ)
      return;

   struct dzn_device *device =
      container_of(templ->base.device, struct dzn_device, vk);

   vk_object_base_finish(&templ->base);
   vk_free2(&device->vk.alloc, alloc, templ);
}

static VkResult
dzn_descriptor_update_template_create(struct dzn_device *device,
                                      const VkDescriptorUpdateTemplateCreateInfo *info,
                                      const VkAllocationCallbacks *alloc,
                                      VkDescriptorUpdateTemplate *out)
{
   assert(info->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET);

   VK_FROM_HANDLE(dzn_descriptor_set_layout, set_layout, info->descriptorSetLayout);

   uint32_t entry_count = 0;
   for (uint32_t e = 0; e < info->descriptorUpdateEntryCount; e++) {
      struct dzn_descriptor_set_ptr ptr;
      dzn_descriptor_set_ptr_init(set_layout, &ptr,
                                  info->pDescriptorUpdateEntries[e].dstBinding,
                                  info->pDescriptorUpdateEntries[e].dstArrayElement);
      uint32_t desc_count = info->pDescriptorUpdateEntries[e].descriptorCount;
      ASSERTED VkDescriptorType type = info->pDescriptorUpdateEntries[e].descriptorType;
      uint32_t d = 0;

      while (dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count) {
         uint32_t ndescs = dzn_descriptor_set_remaining_descs_in_binding(set_layout, &ptr);

         assert(dzn_descriptor_set_ptr_get_vk_type(set_layout, &ptr) == type);
         d += ndescs;
         dzn_descriptor_set_ptr_move(set_layout, &ptr, ndescs);
         entry_count++;
      }

      assert(d >= desc_count);
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct dzn_descriptor_update_template, templ, 1);
   VK_MULTIALLOC_DECL(&ma, struct dzn_descriptor_update_template_entry, entries, entry_count);

   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, alloc,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &templ->base, VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);
   templ->entry_count = entry_count;
   templ->entries = entries;

   struct dzn_descriptor_update_template_entry *entry = entries;

   for (uint32_t e = 0; e < info->descriptorUpdateEntryCount; e++) {
      struct dzn_descriptor_set_ptr ptr;
      dzn_descriptor_set_ptr_init(set_layout, &ptr,
                                  info->pDescriptorUpdateEntries[e].dstBinding,
                                  info->pDescriptorUpdateEntries[e].dstArrayElement);
      uint32_t desc_count = info->pDescriptorUpdateEntries[e].descriptorCount;
      VkDescriptorType type = info->pDescriptorUpdateEntries[e].descriptorType;
      size_t user_data_offset = info->pDescriptorUpdateEntries[e].offset;
      size_t user_data_stride = info->pDescriptorUpdateEntries[e].stride;
      uint32_t d = 0;

      while (dzn_descriptor_set_ptr_is_valid(&ptr) && d < desc_count) {
         uint32_t ndescs = dzn_descriptor_set_remaining_descs_in_binding(set_layout, &ptr);

         entry->type = type;
         entry->desc_count = MIN2(desc_count - d, ndescs);
         entry->user_data.stride = user_data_stride;
         entry->user_data.offset = user_data_offset;
         memset(&entry->heap_offsets, ~0, sizeof(entry->heap_offsets));

         assert(dzn_descriptor_set_ptr_get_vk_type(set_layout, &ptr) == type);
         if (dzn_desc_type_has_sampler(type)) {
            entry->heap_offsets.sampler =
               dzn_descriptor_set_ptr_get_heap_offset(set_layout,
                                                      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                                      &ptr, false);
         }

         if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
             type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
            entry->dynamic_buffer_idx =
               dzn_descriptor_set_ptr_get_dynamic_buffer_idx(set_layout, &ptr);
         } else if (type != VK_DESCRIPTOR_TYPE_SAMPLER) {
            entry->heap_offsets.cbv_srv_uav =
               dzn_descriptor_set_ptr_get_heap_offset(set_layout,
                                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                      &ptr, false);
            if (dzn_descriptor_type_depends_on_shader_usage(type)) {
               entry->heap_offsets.extra_uav =
                  dzn_descriptor_set_ptr_get_heap_offset(set_layout,
                                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                         &ptr, true);
            }
         }

         d += ndescs;
         dzn_descriptor_set_ptr_move(set_layout, &ptr, ndescs);
         user_data_offset += user_data_stride * ndescs;
         ++entry;
      }
   }

   *out = dzn_descriptor_update_template_to_handle(templ);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateDescriptorUpdateTemplate(VkDevice device,
                                   const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   return dzn_descriptor_update_template_create(dzn_device_from_handle(device),
                                                pCreateInfo, pAllocator,
                                                pDescriptorUpdateTemplate);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyDescriptorUpdateTemplate(VkDevice device,
                                    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                    const VkAllocationCallbacks *pAllocator)
{
   dzn_descriptor_update_template_destroy(dzn_descriptor_update_template_from_handle(descriptorUpdateTemplate),
                                          pAllocator);
}

static const void *
dzn_descriptor_update_template_get_desc_data(const struct dzn_descriptor_update_template *templ,
                                             uint32_t e, uint32_t d,
                                             const void *user_data)
{
   return (const void *)((const uint8_t *)user_data +
                         templ->entries[e].user_data.offset +
                         (d * templ->entries[e].user_data.stride));
}

VKAPI_ATTR void VKAPI_CALL
dzn_UpdateDescriptorSetWithTemplate(VkDevice device,
                                    VkDescriptorSet descriptorSet,
                                    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                    const void *pData)
{
   VK_FROM_HANDLE(dzn_descriptor_set, set, descriptorSet);
   VK_FROM_HANDLE(dzn_descriptor_update_template, templ, descriptorUpdateTemplate);

   for (uint32_t e = 0; e < templ->entry_count; e++) {
      const struct dzn_descriptor_update_template_entry *entry = &templ->entries[e];
      bool cube_as_2darray =
         entry->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

      switch (entry->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         for (uint32_t d = 0; d < entry->desc_count; d++) {
            const VkDescriptorImageInfo *info = (const VkDescriptorImageInfo *)
               dzn_descriptor_update_template_get_desc_data(templ, e, d, pData);
            VK_FROM_HANDLE(dzn_sampler, sampler, info->sampler);

            if (sampler)
               dzn_descriptor_set_write_sampler_desc(set, entry->heap_offsets.sampler + d, sampler);
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         for (uint32_t d = 0; d < entry->desc_count; d++) {
            const VkDescriptorImageInfo *info = (const VkDescriptorImageInfo *)
               dzn_descriptor_update_template_get_desc_data(templ, e, d, pData);
            VK_FROM_HANDLE(dzn_sampler, sampler, info->sampler);
            VK_FROM_HANDLE(dzn_image_view, iview, info->imageView);

            if (sampler)
               dzn_descriptor_set_write_sampler_desc(set, entry->heap_offsets.sampler + d, sampler);

            if (iview)
               dzn_descriptor_set_write_image_view_desc(set, entry->heap_offsets.cbv_srv_uav + d, ~0, cube_as_2darray, iview);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         for (uint32_t d = 0; d < entry->desc_count; d++) {
            const VkDescriptorImageInfo *info = (const VkDescriptorImageInfo *)
               dzn_descriptor_update_template_get_desc_data(templ, e, d, pData);
            uint32_t srv_heap_offset = entry->heap_offsets.cbv_srv_uav + d;
            uint32_t uav_heap_offset =
               entry->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ?
               entry->heap_offsets.extra_uav + d : ~0;
            VK_FROM_HANDLE(dzn_image_view, iview, info->imageView);

            if (iview)
               dzn_descriptor_set_write_image_view_desc(set, srv_heap_offset, uav_heap_offset, cube_as_2darray, iview);
         }
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t d = 0; d < entry->desc_count; d++) {
            const VkDescriptorBufferInfo *info = (const VkDescriptorBufferInfo *)
               dzn_descriptor_update_template_get_desc_data(templ, e, d, pData);
            uint32_t cbv_srv_heap_offset = entry->heap_offsets.cbv_srv_uav + d;
            uint32_t uav_heap_offset =
               entry->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ?
               entry->heap_offsets.extra_uav + d : ~0;

            struct dzn_buffer_desc desc = {
               entry->type,
               dzn_buffer_from_handle(info->buffer),
               info->range, info->offset
            };

            if (desc.buffer)
               dzn_descriptor_set_write_buffer_desc(set, cbv_srv_heap_offset, uav_heap_offset, &desc);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t d = 0; d < entry->desc_count; d++) {
            const VkDescriptorBufferInfo *info = (const VkDescriptorBufferInfo *)
               dzn_descriptor_update_template_get_desc_data(templ, e, d, pData);
            uint32_t dyn_buf_idx = entry->dynamic_buffer_idx + d;

            struct dzn_buffer_desc desc = {
               entry->type,
               dzn_buffer_from_handle(info->buffer),
               info->range, info->offset
            };

            if (desc.buffer)
               dzn_descriptor_set_write_dynamic_buffer_desc(set, dyn_buf_idx, &desc);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t d = 0; d < entry->desc_count; d++) {
            VkBufferView *info = (VkBufferView *)
               dzn_descriptor_update_template_get_desc_data(templ, e, d, pData);
            VK_FROM_HANDLE(dzn_buffer_view, bview, *info);
            uint32_t srv_heap_offset = entry->heap_offsets.cbv_srv_uav + d;
            uint32_t uav_heap_offset =
               entry->type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ?
               entry->heap_offsets.extra_uav + d : ~0;

            if (bview)
               dzn_descriptor_set_write_buffer_view_desc(set, srv_heap_offset, uav_heap_offset, bview);
         }
         break;

      default:
         unreachable("invalid descriptor type");
      }
   }
}
