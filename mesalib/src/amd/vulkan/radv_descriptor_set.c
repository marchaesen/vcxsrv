/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>

#include "util/mesa-sha1.h"
#include "radv_acceleration_structure.h"
#include "radv_private.h"
#include "sid.h"
#include "vk_descriptors.h"
#include "vk_format.h"
#include "vk_util.h"

static unsigned
radv_descriptor_type_buffer_count(VkDescriptorType type)
{
   switch (type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
         return 0;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
         return 3;
      default:
         return 1;
   }
}

static bool
has_equal_immutable_samplers(const VkSampler *samplers, uint32_t count)
{
   if (!samplers)
      return false;
   for (uint32_t i = 1; i < count; ++i) {
      if (memcmp(radv_sampler_from_handle(samplers[0])->state,
                 radv_sampler_from_handle(samplers[i])->state, 16)) {
         return false;
      }
   }
   return true;
}

static bool
radv_mutable_descriptor_type_size_alignment(const VkMutableDescriptorTypeListVALVE *list,
                                            uint64_t *out_size, uint64_t *out_align)
{
   uint32_t max_size = 0;
   uint32_t max_align = 0;

   for (uint32_t i = 0; i < list->descriptorTypeCount; i++) {
      uint32_t size = 0;
      uint32_t align = 0;

      switch (list->pDescriptorTypes[i]) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         size = 16;
         align = 16;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         size = 32;
         align = 32;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         size = 64;
         align = 32;
         break;
      default:
         return false;
      }

      max_size = MAX2(max_size, size);
      max_align = MAX2(max_align, align);
   }

   *out_size = max_size;
   *out_align = max_align;
   return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateDescriptorSetLayout(VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkDescriptorSetLayout *pSetLayout)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_descriptor_set_layout *set_layout;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
   const VkDescriptorSetLayoutBindingFlagsCreateInfo *variable_flags =
      vk_find_struct_const(pCreateInfo->pNext, DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
   const VkMutableDescriptorTypeCreateInfoEXT *mutable_info =
      vk_find_struct_const(pCreateInfo->pNext, MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);

   uint32_t num_bindings = 0;
   uint32_t immutable_sampler_count = 0;
   uint32_t ycbcr_sampler_count = 0;
   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      num_bindings = MAX2(num_bindings, pCreateInfo->pBindings[j].binding + 1);
      if ((pCreateInfo->pBindings[j].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           pCreateInfo->pBindings[j].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) &&
          pCreateInfo->pBindings[j].pImmutableSamplers) {
         immutable_sampler_count += pCreateInfo->pBindings[j].descriptorCount;

         bool has_ycbcr_sampler = false;
         for (unsigned i = 0; i < pCreateInfo->pBindings[j].descriptorCount; ++i) {
            if (radv_sampler_from_handle(pCreateInfo->pBindings[j].pImmutableSamplers[i])
                   ->ycbcr_sampler)
               has_ycbcr_sampler = true;
         }

         if (has_ycbcr_sampler)
            ycbcr_sampler_count += pCreateInfo->pBindings[j].descriptorCount;
      }
   }

   uint32_t samplers_offset = offsetof(struct radv_descriptor_set_layout, binding[num_bindings]);
   size_t size = samplers_offset + immutable_sampler_count * 4 * sizeof(uint32_t);
   if (ycbcr_sampler_count > 0) {
      /* Store block of offsets first, followed by the conversion descriptors (padded to the struct
       * alignment) */
      size += num_bindings * sizeof(uint32_t);
      size = ALIGN(size, alignof(struct radv_sampler_ycbcr_conversion_state));
      size += ycbcr_sampler_count * sizeof(struct radv_sampler_ycbcr_conversion_state);
   }

   /* We need to allocate decriptor set layouts off the device allocator with DEVICE scope because
    * they are reference counted and may not be destroyed when vkDestroyDescriptorSetLayout is
    * called.
    */
   set_layout = vk_descriptor_set_layout_zalloc(&device->vk, size);
   if (!set_layout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   set_layout->flags = pCreateInfo->flags;
   set_layout->layout_size = size;

   /* We just allocate all the samplers at the end of the struct */
   uint32_t *samplers = (uint32_t *)&set_layout->binding[num_bindings];
   struct radv_sampler_ycbcr_conversion_state *ycbcr_samplers = NULL;
   uint32_t *ycbcr_sampler_offsets = NULL;

   if (ycbcr_sampler_count > 0) {
      ycbcr_sampler_offsets = samplers + 4 * immutable_sampler_count;
      set_layout->ycbcr_sampler_offsets_offset = (char *)ycbcr_sampler_offsets - (char *)set_layout;

      uintptr_t first_ycbcr_sampler_offset =
         (uintptr_t)ycbcr_sampler_offsets + sizeof(uint32_t) * num_bindings;
      first_ycbcr_sampler_offset =
         ALIGN(first_ycbcr_sampler_offset, alignof(struct radv_sampler_ycbcr_conversion_state));
      ycbcr_samplers = (struct radv_sampler_ycbcr_conversion_state *)first_ycbcr_sampler_offset;
   } else
      set_layout->ycbcr_sampler_offsets_offset = 0;

   VkDescriptorSetLayoutBinding *bindings = NULL;
   VkResult result =
      vk_create_sorted_bindings(pCreateInfo->pBindings, pCreateInfo->bindingCount, &bindings);
   if (result != VK_SUCCESS) {
      vk_descriptor_set_layout_unref(&device->vk, &set_layout->vk);
      return vk_error(device, result);
   }

   set_layout->binding_count = num_bindings;
   set_layout->shader_stages = 0;
   set_layout->dynamic_shader_stages = 0;
   set_layout->has_immutable_samplers = false;
   set_layout->size = 0;

   uint32_t buffer_count = 0;
   uint32_t dynamic_offset_count = 0;

   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + j;
      uint32_t b = binding->binding;
      uint32_t alignment = 0;
      unsigned binding_buffer_count =
         radv_descriptor_type_buffer_count(binding->descriptorType);
      uint32_t descriptor_count = binding->descriptorCount;
      bool has_ycbcr_sampler = false;

      /* main image + fmask */
      uint32_t max_sampled_image_descriptors = 2;

      if (binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
          binding->pImmutableSamplers) {
         for (unsigned i = 0; i < binding->descriptorCount; ++i) {
            struct radv_sampler_ycbcr_conversion *conversion =
               radv_sampler_from_handle(binding->pImmutableSamplers[i])->ycbcr_sampler;

            if (conversion) {
               has_ycbcr_sampler = true;
               max_sampled_image_descriptors = MAX2(max_sampled_image_descriptors,
                                                    vk_format_get_plane_count(conversion->state.format));
            }
         }
      }

      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         assert(!(pCreateInfo->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
         set_layout->binding[b].dynamic_offset_count = 1;
         set_layout->dynamic_shader_stages |= binding->stageFlags;
         if (binding->stageFlags & RADV_RT_STAGE_BITS)
            set_layout->dynamic_shader_stages |= VK_SHADER_STAGE_COMPUTE_BIT;
         set_layout->binding[b].size = 0;
         alignment = 1;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         set_layout->binding[b].size = 16;
         alignment = 16;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         set_layout->binding[b].size = 32;
         alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         /* main descriptor + fmask descriptor */
         set_layout->binding[b].size = 64;
         alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         /* main descriptor + fmask descriptor + sampler */
         set_layout->binding[b].size = 96;
         alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         set_layout->binding[b].size = 16;
         alignment = 16;
         break;
      case VK_DESCRIPTOR_TYPE_MUTABLE_EXT: {
         uint64_t mutable_size = 0, mutable_align = 0;
         radv_mutable_descriptor_type_size_alignment(&mutable_info->pMutableDescriptorTypeLists[j],
                                                     &mutable_size, &mutable_align);
         assert(mutable_size && mutable_align);
         set_layout->binding[b].size = mutable_size;
         alignment = mutable_align;
         break;
      }
      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         alignment = 16;
         set_layout->binding[b].size = descriptor_count;
         descriptor_count = 1;
         break;
      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
         set_layout->binding[b].size = 16;
         alignment = 16;
         break;
      default:
         break;
      }

      set_layout->size = align(set_layout->size, alignment);
      set_layout->binding[b].type = binding->descriptorType;
      set_layout->binding[b].array_size = descriptor_count;
      set_layout->binding[b].offset = set_layout->size;
      set_layout->binding[b].buffer_offset = buffer_count;
      set_layout->binding[b].dynamic_offset_offset = dynamic_offset_count;

      if (variable_flags && binding->binding < variable_flags->bindingCount &&
          (variable_flags->pBindingFlags[binding->binding] &
           VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)) {
         assert(
            !binding->pImmutableSamplers); /* Terribly ill defined  how many samplers are valid */
         assert(binding->binding == num_bindings - 1);

         set_layout->has_variable_descriptors = true;
      }

      if ((binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) &&
          binding->pImmutableSamplers) {
         set_layout->binding[b].immutable_samplers_offset = samplers_offset;
         set_layout->binding[b].immutable_samplers_equal =
            has_equal_immutable_samplers(binding->pImmutableSamplers, binding->descriptorCount);
         set_layout->has_immutable_samplers = true;

         for (uint32_t i = 0; i < binding->descriptorCount; i++)
            memcpy(samplers + 4 * i,
                   &radv_sampler_from_handle(binding->pImmutableSamplers[i])->state, 16);

         /* Don't reserve space for the samplers if they're not accessed. */
         if (set_layout->binding[b].immutable_samplers_equal) {
            if (binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                max_sampled_image_descriptors <= 2)
               set_layout->binding[b].size -= 32;
            else if (binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)
               set_layout->binding[b].size -= 16;
         }
         samplers += 4 * binding->descriptorCount;
         samplers_offset += 4 * sizeof(uint32_t) * binding->descriptorCount;

         if (has_ycbcr_sampler) {
            ycbcr_sampler_offsets[b] = (const char *)ycbcr_samplers - (const char *)set_layout;
            for (uint32_t i = 0; i < binding->descriptorCount; i++) {
               if (radv_sampler_from_handle(binding->pImmutableSamplers[i])->ycbcr_sampler)
                  ycbcr_samplers[i] =
                     radv_sampler_from_handle(binding->pImmutableSamplers[i])->ycbcr_sampler->state;
               else
                  ycbcr_samplers[i].format = VK_FORMAT_UNDEFINED;
            }
            ycbcr_samplers += binding->descriptorCount;
         }
      }

      set_layout->size += descriptor_count * set_layout->binding[b].size;
      buffer_count += descriptor_count * binding_buffer_count;
      dynamic_offset_count += descriptor_count * set_layout->binding[b].dynamic_offset_count;
      set_layout->shader_stages |= binding->stageFlags;
   }

   free(bindings);

   set_layout->buffer_count = buffer_count;
   set_layout->dynamic_offset_count = dynamic_offset_count;

   *pSetLayout = radv_descriptor_set_layout_to_handle(set_layout);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDescriptorSetLayoutSupport(VkDevice device,
                                   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                   VkDescriptorSetLayoutSupport *pSupport)
{
   VkDescriptorSetLayoutBinding *bindings = NULL;
   VkResult result =
      vk_create_sorted_bindings(pCreateInfo->pBindings, pCreateInfo->bindingCount, &bindings);
   if (result != VK_SUCCESS) {
      pSupport->supported = false;
      return;
   }

   const VkDescriptorSetLayoutBindingFlagsCreateInfo *variable_flags =
      vk_find_struct_const(pCreateInfo->pNext, DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
   VkDescriptorSetVariableDescriptorCountLayoutSupport *variable_count = vk_find_struct(
      (void *)pCreateInfo->pNext, DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT);
   const VkMutableDescriptorTypeCreateInfoEXT *mutable_info =
      vk_find_struct_const(pCreateInfo->pNext, MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);
   if (variable_count) {
      variable_count->maxVariableDescriptorCount = 0;
   }

   bool supported = true;
   uint64_t size = 0;
   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + i;

      uint64_t descriptor_size = 0;
      uint64_t descriptor_alignment = 1;
      uint32_t descriptor_count = binding->descriptorCount;
      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         descriptor_size = 16;
         descriptor_alignment = 16;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         descriptor_size = 32;
         descriptor_alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         descriptor_size = 64;
         descriptor_alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         if (!has_equal_immutable_samplers(binding->pImmutableSamplers, descriptor_count)) {
            descriptor_size = 64;
         } else {
            descriptor_size = 96;
         }
         descriptor_alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         if (!has_equal_immutable_samplers(binding->pImmutableSamplers, descriptor_count)) {
            descriptor_size = 16;
            descriptor_alignment = 16;
         }
         break;
      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         descriptor_alignment = 16;
         descriptor_size = descriptor_count;
         descriptor_count = 1;
         break;
      case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
         if (!radv_mutable_descriptor_type_size_alignment(
                &mutable_info->pMutableDescriptorTypeLists[i], &descriptor_size,
                &descriptor_alignment)) {
            supported = false;
         }
         break;
      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
         descriptor_size = 16;
         descriptor_alignment = 16;
         break;
      default:
         break;
      }

      if (size && !align_u64(size, descriptor_alignment)) {
         supported = false;
      }
      size = align_u64(size, descriptor_alignment);

      uint64_t max_count = INT32_MAX;
      if (binding->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
         max_count = INT32_MAX - size;
      else if (descriptor_size)
         max_count = (INT32_MAX - size) / descriptor_size;

      if (max_count < descriptor_count) {
         supported = false;
      }
      if (variable_flags && binding->binding < variable_flags->bindingCount && variable_count &&
          (variable_flags->pBindingFlags[binding->binding] &
           VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)) {
         variable_count->maxVariableDescriptorCount = MIN2(UINT32_MAX, max_count);
      }
      size += descriptor_count * descriptor_size;
   }

   free(bindings);

   pSupport->supported = supported;
}

/*
 * Pipeline layouts.  These have nothing to do with the pipeline.  They are
 * just multiple descriptor set layouts pasted together.
 */
void
radv_pipeline_layout_init(struct radv_device *device, struct radv_pipeline_layout *layout,
                          bool independent_sets)
{
   memset(layout, 0, sizeof(*layout));

   vk_object_base_init(&device->vk, &layout->base, VK_OBJECT_TYPE_PIPELINE_LAYOUT);

   layout->independent_sets = independent_sets;
}

void
radv_pipeline_layout_add_set(struct radv_pipeline_layout *layout, uint32_t set_idx,
                             struct radv_descriptor_set_layout *set_layout)
{
   unsigned dynamic_offset_count = 0;

   if (layout->set[set_idx].layout)
      return;

   layout->num_sets = MAX2(set_idx + 1, layout->num_sets);

   layout->set[set_idx].layout = set_layout;
   vk_descriptor_set_layout_ref(&set_layout->vk);

   for (uint32_t b = 0; b < set_layout->binding_count; b++) {
      dynamic_offset_count += set_layout->binding[b].array_size * set_layout->binding[b].dynamic_offset_count;
   }

   layout->set[set_idx].dynamic_offset_start = layout->dynamic_offset_count;

   layout->dynamic_offset_count += dynamic_offset_count;
   layout->dynamic_shader_stages |= set_layout->dynamic_shader_stages;
}

void
radv_pipeline_layout_hash(struct radv_pipeline_layout *layout)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);
   for (uint32_t i = 0; i < layout->num_sets; i++) {
      struct radv_descriptor_set_layout *set_layout = layout->set[i].layout;

      if (!set_layout)
         continue;

      /* Hash the entire set layout except for the vk_object_base and the reference counter. The
       * rest of the set layout is carefully constructed to not have pointers so a full hash instead
       * of a per-field hash should be ok.
       */
      uint32_t hash_offset = sizeof(struct vk_object_base) + sizeof(uint32_t);
      _mesa_sha1_update(&ctx, (const char *)set_layout + hash_offset,
                        set_layout->layout_size - hash_offset);
   }
   _mesa_sha1_update(&ctx, &layout->push_constant_size, sizeof(layout->push_constant_size));
   _mesa_sha1_final(&ctx, layout->sha1);
}

void
radv_pipeline_layout_finish(struct radv_device *device, struct radv_pipeline_layout *layout)
{
   for (uint32_t i = 0; i < layout->num_sets; i++) {
      if (!layout->set[i].layout)
         continue;

      vk_descriptor_set_layout_unref(&device->vk, &layout->set[i].layout->vk);
   }

   vk_object_base_finish(&layout->base);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreatePipelineLayout(VkDevice _device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipelineLayout *pPipelineLayout)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_pipeline_layout *layout;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

   layout = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*layout), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (layout == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_layout_init(device, layout,
                             pCreateInfo->flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);

   layout->num_sets = pCreateInfo->setLayoutCount;

   for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
      RADV_FROM_HANDLE(radv_descriptor_set_layout, set_layout, pCreateInfo->pSetLayouts[set]);

      if (set_layout == NULL) {
         layout->set[set].layout = NULL;
         continue;
      }

      radv_pipeline_layout_add_set(layout, set, set_layout);
   }

   layout->push_constant_size = 0;

   for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
      const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + i;
      layout->push_constant_size = MAX2(layout->push_constant_size, range->offset + range->size);
   }

   layout->push_constant_size = align(layout->push_constant_size, 16);

   radv_pipeline_layout_hash(layout);

   *pPipelineLayout = radv_pipeline_layout_to_handle(layout);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyPipelineLayout(VkDevice _device, VkPipelineLayout _pipelineLayout,
                           const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, _pipelineLayout);

   if (!pipeline_layout)
      return;

   radv_pipeline_layout_finish(device, pipeline_layout);

   vk_free2(&device->vk.alloc, pAllocator, pipeline_layout);
}

static VkResult
radv_descriptor_set_create(struct radv_device *device, struct radv_descriptor_pool *pool,
                           struct radv_descriptor_set_layout *layout, const uint32_t *variable_count,
                           struct radv_descriptor_set **out_set)
{
   if (pool->entry_count == pool->max_entry_count)
      return VK_ERROR_OUT_OF_POOL_MEMORY;

   struct radv_descriptor_set *set;
   uint32_t buffer_count = layout->buffer_count;
   if (variable_count) {
      unsigned stride =
         radv_descriptor_type_buffer_count(layout->binding[layout->binding_count - 1].type);
      buffer_count =
         layout->binding[layout->binding_count - 1].buffer_offset + *variable_count * stride;
   }
   unsigned range_offset =
      sizeof(struct radv_descriptor_set_header) + sizeof(struct radeon_winsys_bo *) * buffer_count;
   const unsigned dynamic_offset_count = layout->dynamic_offset_count;
   unsigned mem_size =
      range_offset + sizeof(struct radv_descriptor_range) * dynamic_offset_count;

   if (pool->host_memory_base) {
      if (pool->host_memory_end - pool->host_memory_ptr < mem_size)
         return VK_ERROR_OUT_OF_POOL_MEMORY;

      set = (struct radv_descriptor_set *)pool->host_memory_ptr;
      pool->host_memory_ptr += mem_size;
   } else {
      set = vk_alloc2(&device->vk.alloc, NULL, mem_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      if (!set)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   memset(set, 0, mem_size);

   vk_object_base_init(&device->vk, &set->header.base, VK_OBJECT_TYPE_DESCRIPTOR_SET);

   if (dynamic_offset_count) {
      set->header.dynamic_descriptors =
         (struct radv_descriptor_range *)((uint8_t *)set + range_offset);
   }

   set->header.layout = layout;
   set->header.buffer_count = buffer_count;
   uint32_t layout_size = layout->size;
   if (variable_count) {
      uint32_t stride = layout->binding[layout->binding_count - 1].size;
      if (layout->binding[layout->binding_count - 1].type ==
          VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
         stride = 1;

      layout_size = layout->binding[layout->binding_count - 1].offset + *variable_count * stride;
   }
   layout_size = align_u32(layout_size, 32);
   set->header.size = layout_size;

   /* try to allocate linearly first, so that we don't spend
    * time looking for gaps if the app only allocates &
    * resets via the pool. */
   if (pool->current_offset + layout_size <= pool->size) {
      set->header.bo = pool->bo;
      set->header.mapped_ptr = (uint32_t *)(pool->mapped_ptr + pool->current_offset);
      set->header.va = pool->bo ? (radv_buffer_get_va(set->header.bo) + pool->current_offset) : 0;

      if (!pool->host_memory_base) {
         pool->entries[pool->entry_count].offset = pool->current_offset;
         pool->entries[pool->entry_count].size = layout_size;
         pool->entries[pool->entry_count].set = set;
      } else {
         pool->layouts[pool->entry_count] = layout;
      }

      pool->current_offset += layout_size;
   } else if (!pool->host_memory_base) {
      uint64_t offset = 0;
      int index;

      for (index = 0; index < pool->entry_count; ++index) {
         if (pool->entries[index].offset - offset >= layout_size)
            break;
         offset = pool->entries[index].offset + pool->entries[index].size;
      }

      if (pool->size - offset < layout_size) {
         vk_free2(&device->vk.alloc, NULL, set);
         return VK_ERROR_OUT_OF_POOL_MEMORY;
      }
      set->header.bo = pool->bo;
      set->header.mapped_ptr = (uint32_t *)(pool->mapped_ptr + offset);
      set->header.va = pool->bo ? (radv_buffer_get_va(set->header.bo) + offset) : 0;
      memmove(&pool->entries[index + 1], &pool->entries[index],
              sizeof(pool->entries[0]) * (pool->entry_count - index));
      pool->entries[index].offset = offset;
      pool->entries[index].size = layout_size;
      pool->entries[index].set = set;
   } else
      return VK_ERROR_OUT_OF_POOL_MEMORY;

   if (layout->has_immutable_samplers) {
      for (unsigned i = 0; i < layout->binding_count; ++i) {
         if (!layout->binding[i].immutable_samplers_offset ||
             layout->binding[i].immutable_samplers_equal)
            continue;

         unsigned offset = layout->binding[i].offset / 4;
         if (layout->binding[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            offset += radv_combined_image_descriptor_sampler_offset(layout->binding + i) / 4;

         const uint32_t *samplers =
            (const uint32_t *)((const char *)layout + layout->binding[i].immutable_samplers_offset);
         for (unsigned j = 0; j < layout->binding[i].array_size; ++j) {
            memcpy(set->header.mapped_ptr + offset, samplers + 4 * j, 16);
            offset += layout->binding[i].size / 4;
         }
      }
   }

   pool->entry_count++;
   vk_descriptor_set_layout_ref(&layout->vk);
   *out_set = set;
   return VK_SUCCESS;
}

static void
radv_descriptor_set_destroy(struct radv_device *device, struct radv_descriptor_pool *pool,
                            struct radv_descriptor_set *set, bool free_bo)
{
   assert(!pool->host_memory_base);

   vk_descriptor_set_layout_unref(&device->vk, &set->header.layout->vk);

   if (free_bo && !pool->host_memory_base) {
      for (int i = 0; i < pool->entry_count; ++i) {
         if (pool->entries[i].set == set) {
            memmove(&pool->entries[i], &pool->entries[i + 1],
                    sizeof(pool->entries[i]) * (pool->entry_count - i - 1));
            --pool->entry_count;
            break;
         }
      }
   }
   vk_object_base_finish(&set->header.base);
   vk_free2(&device->vk.alloc, NULL, set);
}

static void
radv_destroy_descriptor_pool(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                             struct radv_descriptor_pool *pool)
{

   if (!pool->host_memory_base) {
      for (uint32_t i = 0; i < pool->entry_count; ++i) {
         radv_descriptor_set_destroy(device, pool, pool->entries[i].set, false);
      }
   } else {
      for (uint32_t i = 0; i < pool->entry_count; ++i) {
         vk_descriptor_set_layout_unref(&device->vk, &pool->layouts[i]->vk);
      }
   }

   if (pool->bo)
      device->ws->buffer_destroy(device->ws, pool->bo);
   if (pool->host_bo)
      vk_free2(&device->vk.alloc, pAllocator, pool->host_bo);

   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateDescriptorPool(VkDevice _device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkDescriptorPool *pDescriptorPool)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_descriptor_pool *pool;
   uint64_t size = sizeof(struct radv_descriptor_pool);
   uint64_t bo_size = 0, bo_count = 0, range_count = 0;

   const VkMutableDescriptorTypeCreateInfoEXT *mutable_info =
      vk_find_struct_const(pCreateInfo->pNext, MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);

   vk_foreach_struct_const(ext, pCreateInfo->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO: {
         const VkDescriptorPoolInlineUniformBlockCreateInfo *info =
            (const VkDescriptorPoolInlineUniformBlockCreateInfo *)ext;
         /* the sizes are 4 aligned, and we need to align to at
          * most 32, which needs at most 28 bytes extra per
          * binding. */
         bo_size += 28llu * info->maxInlineUniformBlockBindings;
         break;
      }
      default:
         break;
      }
   }

   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      bo_count += radv_descriptor_type_buffer_count(pCreateInfo->pPoolSizes[i].type) *
         pCreateInfo->pPoolSizes[i].descriptorCount;

      switch (pCreateInfo->pPoolSizes[i].type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         range_count += pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         /* 32 as we may need to align for images */
         bo_size += 32 * pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         bo_size += 64 * pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
         /* Per spec, if a mutable descriptor type list is provided for the pool entry, we
          * allocate enough memory to hold any subset of that list.
          * If there is no mutable descriptor type list available,
          * we must allocate enough for any supported mutable descriptor type, i.e. 64 bytes. */
         if (mutable_info && i < mutable_info->mutableDescriptorTypeListCount) {
            uint64_t mutable_size, mutable_alignment;
            if (radv_mutable_descriptor_type_size_alignment(
                   &mutable_info->pMutableDescriptorTypeLists[i], &mutable_size,
                   &mutable_alignment)) {
               /* 32 as we may need to align for images */
               mutable_size = align(mutable_size, 32);
               bo_size += mutable_size * pCreateInfo->pPoolSizes[i].descriptorCount;
            }
         } else {
            bo_size += 64 * pCreateInfo->pPoolSizes[i].descriptorCount;
         }
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         bo_size += 96 * pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         bo_size += pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      default:
         break;
      }
   }

   uint64_t layouts_size = 0;

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      size += pCreateInfo->maxSets * sizeof(struct radv_descriptor_set);
      size += sizeof(struct radeon_winsys_bo *) * bo_count;
      size += sizeof(struct radv_descriptor_range) * range_count;

      layouts_size = sizeof(struct radv_descriptor_set_layout *) * pCreateInfo->maxSets;
      size += layouts_size;
   } else {
      size += sizeof(struct radv_descriptor_pool_entry) * pCreateInfo->maxSets;
   }

   pool = vk_alloc2(&device->vk.alloc, pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(pool, 0, sizeof(*pool));

   vk_object_base_init(&device->vk, &pool->base, VK_OBJECT_TYPE_DESCRIPTOR_POOL);

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      pool->host_memory_base = (uint8_t *)pool + sizeof(struct radv_descriptor_pool) + layouts_size;
      pool->host_memory_ptr = pool->host_memory_base;
      pool->host_memory_end = (uint8_t *)pool + size;
   }

   if (bo_size) {
      if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_VALVE)) {
         enum radeon_bo_flag flags = RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY |
                                     RADEON_FLAG_32BIT;

         if (device->instance->zero_vram)
            flags |= RADEON_FLAG_ZERO_VRAM;

         VkResult result = device->ws->buffer_create(
            device->ws, bo_size, 32, RADEON_DOMAIN_VRAM, flags, RADV_BO_PRIORITY_DESCRIPTOR, 0,
            &pool->bo);
         if (result != VK_SUCCESS) {
            radv_destroy_descriptor_pool(device, pAllocator, pool);
            return vk_error(device, result);
         }
         pool->mapped_ptr = (uint8_t *)device->ws->buffer_map(pool->bo);
         if (!pool->mapped_ptr) {
            radv_destroy_descriptor_pool(device, pAllocator, pool);
            return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         }
      } else {
         pool->host_bo =
            vk_alloc2(&device->vk.alloc, pAllocator, bo_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (!pool->host_bo) {
            radv_destroy_descriptor_pool(device, pAllocator, pool);
            return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
         }
         pool->mapped_ptr = pool->host_bo;
      }
   }
   pool->size = bo_size;
   pool->max_entry_count = pCreateInfo->maxSets;

   *pDescriptorPool = radv_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyDescriptorPool(VkDevice _device, VkDescriptorPool _pool,
                           const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_descriptor_pool, pool, _pool);

   if (!pool)
      return;

   radv_destroy_descriptor_pool(device, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_ResetDescriptorPool(VkDevice _device, VkDescriptorPool descriptorPool,
                         VkDescriptorPoolResetFlags flags)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_descriptor_pool, pool, descriptorPool);

   if (!pool->host_memory_base) {
      for (uint32_t i = 0; i < pool->entry_count; ++i) {
         radv_descriptor_set_destroy(device, pool, pool->entries[i].set, false);
      }
   } else {
      for (uint32_t i = 0; i < pool->entry_count; ++i) {
         vk_descriptor_set_layout_unref(&device->vk, &pool->layouts[i]->vk);
      }
   }

   pool->entry_count = 0;

   pool->current_offset = 0;
   pool->host_memory_ptr = pool->host_memory_base;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_AllocateDescriptorSets(VkDevice _device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                            VkDescriptorSet *pDescriptorSets)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_descriptor_pool, pool, pAllocateInfo->descriptorPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;
   struct radv_descriptor_set *set = NULL;

   const VkDescriptorSetVariableDescriptorCountAllocateInfo *variable_counts = vk_find_struct_const(
      pAllocateInfo->pNext, DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
   const uint32_t zero = 0;

   /* allocate a set of buffers for each shader to contain descriptors */
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      RADV_FROM_HANDLE(radv_descriptor_set_layout, layout, pAllocateInfo->pSetLayouts[i]);

      const uint32_t *variable_count = NULL;
      if (layout->has_variable_descriptors && variable_counts) {
         if (i < variable_counts->descriptorSetCount)
            variable_count = variable_counts->pDescriptorCounts + i;
         else
            variable_count = &zero;
      }

      assert(!(layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));

      result = radv_descriptor_set_create(device, pool, layout, variable_count, &set);
      if (result != VK_SUCCESS)
         break;

      pDescriptorSets[i] = radv_descriptor_set_to_handle(set);
   }

   if (result != VK_SUCCESS) {
      radv_FreeDescriptorSets(_device, pAllocateInfo->descriptorPool, i, pDescriptorSets);
      for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
         pDescriptorSets[i] = VK_NULL_HANDLE;
      }
   }
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_FreeDescriptorSets(VkDevice _device, VkDescriptorPool descriptorPool, uint32_t count,
                        const VkDescriptorSet *pDescriptorSets)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_descriptor_pool, pool, descriptorPool);

   for (uint32_t i = 0; i < count; i++) {
      RADV_FROM_HANDLE(radv_descriptor_set, set, pDescriptorSets[i]);

      if (set && !pool->host_memory_base)
         radv_descriptor_set_destroy(device, pool, set, true);
   }
   return VK_SUCCESS;
}

static ALWAYS_INLINE void
write_texel_buffer_descriptor(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                              unsigned *dst, struct radeon_winsys_bo **buffer_list,
                              const VkBufferView _buffer_view)
{
   RADV_FROM_HANDLE(radv_buffer_view, buffer_view, _buffer_view);

   if (!buffer_view) {
      memset(dst, 0, 4 * 4);
      if (!cmd_buffer)
         *buffer_list = NULL;
      return;
   }

   memcpy(dst, buffer_view->state, 4 * 4);

   if (cmd_buffer)
      radv_cs_add_buffer(device->ws, cmd_buffer->cs, buffer_view->bo);
   else
      *buffer_list = buffer_view->bo;
}

static ALWAYS_INLINE void
write_buffer_descriptor(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                        unsigned *dst, struct radeon_winsys_bo **buffer_list,
                        const VkDescriptorBufferInfo *buffer_info)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);

   if (!buffer) {
      memset(dst, 0, 4 * 4);
      if (!cmd_buffer)
         *buffer_list = NULL;
      return;
   }

   uint64_t va = radv_buffer_get_va(buffer->bo);

   uint32_t range = vk_buffer_range(&buffer->vk, buffer_info->offset, buffer_info->range);
   assert(buffer->vk.size > 0 && range > 0);

   /* robustBufferAccess is relaxed enough to allow this (in combination
    * with the alignment/size we return from vkGetBufferMemoryRequirements)
    * and this allows the shader compiler to create more efficient 8/16-bit
    * buffer accesses. */
   range = align(range, 4);

   va += buffer_info->offset + buffer->offset;

   uint32_t rsrc_word3 =
      S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
      S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

   if (device->physical_device->rad_info.gfx_level >= GFX11) {
      rsrc_word3 |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW);
   } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
      rsrc_word3 |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
   } else {
      rsrc_word3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
   }

   dst[0] = va;
   dst[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
   dst[2] = range;
   dst[3] = rsrc_word3;

   if (cmd_buffer)
      radv_cs_add_buffer(device->ws, cmd_buffer->cs, buffer->bo);
   else
      *buffer_list = buffer->bo;
}

static ALWAYS_INLINE void
write_block_descriptor(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer, void *dst,
                       const VkWriteDescriptorSet *writeset)
{
   const VkWriteDescriptorSetInlineUniformBlock *inline_ub =
      vk_find_struct_const(writeset->pNext, WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);

   memcpy(dst, inline_ub->pData, inline_ub->dataSize);
}

static ALWAYS_INLINE void
write_dynamic_buffer_descriptor(struct radv_device *device, struct radv_descriptor_range *range,
                                struct radeon_winsys_bo **buffer_list,
                                const VkDescriptorBufferInfo *buffer_info)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);
   uint64_t va;
   unsigned size;

   if (!buffer) {
      range->va = 0;
      *buffer_list = NULL;
      return;
   }

   va = radv_buffer_get_va(buffer->bo);

   size = vk_buffer_range(&buffer->vk, buffer_info->offset, buffer_info->range);
   assert(buffer->vk.size > 0 && size > 0);

   /* robustBufferAccess is relaxed enough to allow this (in combination
    * with the alignment/size we return from vkGetBufferMemoryRequirements)
    * and this allows the shader compiler to create more efficient 8/16-bit
    * buffer accesses. */
   size = align(size, 4);

   va += buffer_info->offset + buffer->offset;
   range->va = va;
   range->size = size;

   *buffer_list = buffer->bo;
}

static ALWAYS_INLINE void
write_image_descriptor(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                       unsigned size, unsigned *dst, struct radeon_winsys_bo **buffer_list,
                       VkDescriptorType descriptor_type, const VkDescriptorImageInfo *image_info)
{
   RADV_FROM_HANDLE(radv_image_view, iview, image_info->imageView);
   union radv_descriptor *descriptor;

   if (!iview) {
      memset(dst, 0, size);
      if (!cmd_buffer)
         *buffer_list = NULL;
      return;
   }

   if (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
      descriptor = &iview->storage_descriptor;
   } else {
      descriptor = &iview->descriptor;
   }
   assert(size > 0);

   memcpy(dst, descriptor, size);

   const uint32_t max_bindings = sizeof(iview->image->bindings) /
                                 sizeof(iview->image->bindings[0]);
   for (uint32_t b = 0; b < max_bindings; b++) {
      if (cmd_buffer) {
         if (iview->image->bindings[b].bo)
            radv_cs_add_buffer(device->ws, cmd_buffer->cs, iview->image->bindings[b].bo);
      } else {
         *buffer_list = iview->image->bindings[b].bo;
         buffer_list++;
      }
   }
}

static ALWAYS_INLINE void
write_combined_image_sampler_descriptor(struct radv_device *device,
                                        struct radv_cmd_buffer *cmd_buffer, unsigned sampler_offset,
                                        unsigned *dst, struct radeon_winsys_bo **buffer_list,
                                        VkDescriptorType descriptor_type,
                                        const VkDescriptorImageInfo *image_info, bool has_sampler)
{
   write_image_descriptor(device, cmd_buffer, sampler_offset, dst, buffer_list, descriptor_type,
                          image_info);
   /* copy over sampler state */
   if (has_sampler) {
      RADV_FROM_HANDLE(radv_sampler, sampler, image_info->sampler);
      memcpy(dst + sampler_offset / sizeof(*dst), sampler->state, 16);
   }
}

static ALWAYS_INLINE void
write_sampler_descriptor(unsigned *dst, VkSampler _sampler)
{
   RADV_FROM_HANDLE(radv_sampler, sampler, _sampler);
   memcpy(dst, sampler->state, 16);
}

static ALWAYS_INLINE void
write_accel_struct(void *ptr, VkAccelerationStructureKHR _accel_struct)
{
   RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct, _accel_struct);
   uint64_t va = accel_struct ? accel_struct->va : 0;
   memcpy(ptr, &va, sizeof(va));
}

static ALWAYS_INLINE void
radv_update_descriptor_sets_impl(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                                 VkDescriptorSet dstSetOverride, uint32_t descriptorWriteCount,
                                 const VkWriteDescriptorSet *pDescriptorWrites,
                                 uint32_t descriptorCopyCount,
                                 const VkCopyDescriptorSet *pDescriptorCopies)
{
   uint32_t i, j;
   for (i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *writeset = &pDescriptorWrites[i];
      RADV_FROM_HANDLE(radv_descriptor_set, set,
                       dstSetOverride ? dstSetOverride : writeset->dstSet);
      const struct radv_descriptor_set_binding_layout *binding_layout =
         set->header.layout->binding + writeset->dstBinding;
      uint32_t *ptr = set->header.mapped_ptr;
      struct radeon_winsys_bo **buffer_list = set->descriptors;
      /* Immutable samplers are not copied into push descriptors when they are
       * allocated, so if we are writing push descriptors we have to copy the
       * immutable samplers into them now.
       */
      const bool copy_immutable_samplers = cmd_buffer &&
                                           binding_layout->immutable_samplers_offset &&
                                           !binding_layout->immutable_samplers_equal;
      const uint32_t *samplers = radv_immutable_samplers(set->header.layout, binding_layout);
      const VkWriteDescriptorSetAccelerationStructureKHR *accel_structs = NULL;

      ptr += binding_layout->offset / 4;

      if (writeset->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         write_block_descriptor(device, cmd_buffer, (uint8_t *)ptr + writeset->dstArrayElement,
                                writeset);
         continue;
      } else if (writeset->descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
         accel_structs =
            vk_find_struct_const(writeset->pNext, WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
      }

      ptr += binding_layout->size * writeset->dstArrayElement / 4;
      buffer_list += binding_layout->buffer_offset;
      buffer_list += writeset->dstArrayElement;
      for (j = 0; j < writeset->descriptorCount; ++j) {
         switch (writeset->descriptorType) {
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            unsigned idx = writeset->dstArrayElement + j;
            idx += binding_layout->dynamic_offset_offset;
            assert(!(set->header.layout->flags &
                     VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
            write_dynamic_buffer_descriptor(device, set->header.dynamic_descriptors + idx,
                                            buffer_list, writeset->pBufferInfo + j);
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            write_buffer_descriptor(device, cmd_buffer, ptr, buffer_list,
                                    writeset->pBufferInfo + j);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            write_texel_buffer_descriptor(device, cmd_buffer, ptr, buffer_list,
                                          writeset->pTexelBufferView[j]);
            break;
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            write_image_descriptor(device, cmd_buffer, 32, ptr, buffer_list,
                                   writeset->descriptorType, writeset->pImageInfo + j);
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            write_image_descriptor(device, cmd_buffer, 64, ptr, buffer_list,
                                   writeset->descriptorType, writeset->pImageInfo + j);
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            unsigned sampler_offset = radv_combined_image_descriptor_sampler_offset(binding_layout);
            write_combined_image_sampler_descriptor(
               device, cmd_buffer, sampler_offset, ptr, buffer_list, writeset->descriptorType,
               writeset->pImageInfo + j, !binding_layout->immutable_samplers_offset);
            if (copy_immutable_samplers) {
               const unsigned idx = writeset->dstArrayElement + j;
               memcpy((char *)ptr + sampler_offset, samplers + 4 * idx, 16);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            if (!binding_layout->immutable_samplers_offset) {
               const VkDescriptorImageInfo *pImageInfo = writeset->pImageInfo + j;
               write_sampler_descriptor(ptr, pImageInfo->sampler);
            } else if (copy_immutable_samplers) {
               unsigned idx = writeset->dstArrayElement + j;
               memcpy(ptr, samplers + 4 * idx, 16);
            }
            break;
         case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            write_accel_struct(ptr, accel_structs->pAccelerationStructures[j]);
            break;
         default:
            break;
         }
         ptr += binding_layout->size / 4;
         ++buffer_list;
      }
   }

   for (i = 0; i < descriptorCopyCount; i++) {
      const VkCopyDescriptorSet *copyset = &pDescriptorCopies[i];
      RADV_FROM_HANDLE(radv_descriptor_set, src_set, copyset->srcSet);
      RADV_FROM_HANDLE(radv_descriptor_set, dst_set, copyset->dstSet);
      const struct radv_descriptor_set_binding_layout *src_binding_layout =
         src_set->header.layout->binding + copyset->srcBinding;
      const struct radv_descriptor_set_binding_layout *dst_binding_layout =
         dst_set->header.layout->binding + copyset->dstBinding;
      uint32_t *src_ptr = src_set->header.mapped_ptr;
      uint32_t *dst_ptr = dst_set->header.mapped_ptr;
      struct radeon_winsys_bo **src_buffer_list = src_set->descriptors;
      struct radeon_winsys_bo **dst_buffer_list = dst_set->descriptors;

      src_ptr += src_binding_layout->offset / 4;
      dst_ptr += dst_binding_layout->offset / 4;

      if (src_binding_layout->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         src_ptr += copyset->srcArrayElement / 4;
         dst_ptr += copyset->dstArrayElement / 4;

         memcpy(dst_ptr, src_ptr, copyset->descriptorCount);
         continue;
      }

      src_ptr += src_binding_layout->size * copyset->srcArrayElement / 4;
      dst_ptr += dst_binding_layout->size * copyset->dstArrayElement / 4;

      src_buffer_list += src_binding_layout->buffer_offset;
      src_buffer_list += copyset->srcArrayElement;

      dst_buffer_list += dst_binding_layout->buffer_offset;
      dst_buffer_list += copyset->dstArrayElement;

      /* In case of copies between mutable descriptor types
       * and non-mutable descriptor types. */
      size_t copy_size = MIN2(src_binding_layout->size, dst_binding_layout->size);

      for (j = 0; j < copyset->descriptorCount; ++j) {
         switch (src_binding_layout->type) {
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            unsigned src_idx = copyset->srcArrayElement + j;
            unsigned dst_idx = copyset->dstArrayElement + j;
            struct radv_descriptor_range *src_range, *dst_range;
            src_idx += src_binding_layout->dynamic_offset_offset;
            dst_idx += dst_binding_layout->dynamic_offset_offset;

            src_range = src_set->header.dynamic_descriptors + src_idx;
            dst_range = dst_set->header.dynamic_descriptors + dst_idx;
            *dst_range = *src_range;
            break;
         }
         default:
            memcpy(dst_ptr, src_ptr, copy_size);
         }
         src_ptr += src_binding_layout->size / 4;
         dst_ptr += dst_binding_layout->size / 4;

         unsigned src_buffer_count = radv_descriptor_type_buffer_count(src_binding_layout->type);
         unsigned dst_buffer_count = radv_descriptor_type_buffer_count(dst_binding_layout->type);
         for (unsigned k = 0; k < dst_buffer_count; k++) {
            if (k < src_buffer_count)
               dst_buffer_list[k] = src_buffer_list[k];
            else
               dst_buffer_list[k] = NULL;
         }

         dst_buffer_list += dst_buffer_count;
         src_buffer_list += src_buffer_count;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_UpdateDescriptorSets(VkDevice _device, uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   radv_update_descriptor_sets_impl(device, NULL, VK_NULL_HANDLE, descriptorWriteCount,
                                    pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

void
radv_cmd_update_descriptor_sets(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                                VkDescriptorSet dstSetOverride, uint32_t descriptorWriteCount,
                                const VkWriteDescriptorSet *pDescriptorWrites,
                                uint32_t descriptorCopyCount,
                                const VkCopyDescriptorSet *pDescriptorCopies)
{
   /* Assume cmd_buffer != NULL to optimize out cmd_buffer checks in generic code above. */
   assume(cmd_buffer != NULL);
   radv_update_descriptor_sets_impl(device, cmd_buffer, dstSetOverride, descriptorWriteCount,
                                    pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateDescriptorUpdateTemplate(VkDevice _device,
                                    const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   const uint32_t entry_count = pCreateInfo->descriptorUpdateEntryCount;
   const size_t size = sizeof(struct radv_descriptor_update_template) +
                       sizeof(struct radv_descriptor_update_template_entry) * entry_count;
   struct radv_descriptor_set_layout *set_layout = NULL;
   struct radv_descriptor_update_template *templ;
   uint32_t i;

   templ = vk_alloc2(&device->vk.alloc, pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!templ)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &templ->base, VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);

   templ->entry_count = entry_count;

   if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
      RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->pipelineLayout);

      /* descriptorSetLayout should be ignored for push descriptors
       * and instead it refers to pipelineLayout and set.
       */
      assert(pCreateInfo->set < MAX_SETS);
      set_layout = pipeline_layout->set[pCreateInfo->set].layout;

      templ->bind_point = pCreateInfo->pipelineBindPoint;
   } else {
      assert(pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET);
      set_layout = radv_descriptor_set_layout_from_handle(pCreateInfo->descriptorSetLayout);
   }

   for (i = 0; i < entry_count; i++) {
      const VkDescriptorUpdateTemplateEntry *entry = &pCreateInfo->pDescriptorUpdateEntries[i];
      const struct radv_descriptor_set_binding_layout *binding_layout =
         set_layout->binding + entry->dstBinding;
      const uint32_t buffer_offset = binding_layout->buffer_offset + entry->dstArrayElement;
      const uint32_t *immutable_samplers = NULL;
      uint32_t dst_offset;
      uint32_t dst_stride;

      /* dst_offset is an offset into dynamic_descriptors when the descriptor
         is dynamic, and an offset into mapped_ptr otherwise */
      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         assert(pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET);
         dst_offset = binding_layout->dynamic_offset_offset + entry->dstArrayElement;
         dst_stride = 0; /* Not used */
         break;
      default:
         switch (entry->descriptorType) {
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            /* Immutable samplers are copied into push descriptors when they are pushed */
            if (pCreateInfo->templateType ==
                   VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR &&
                binding_layout->immutable_samplers_offset &&
                !binding_layout->immutable_samplers_equal) {
               immutable_samplers =
                  radv_immutable_samplers(set_layout, binding_layout) + entry->dstArrayElement * 4;
            }
            break;
         default:
            break;
         }
         dst_offset = binding_layout->offset / 4;
         if (entry->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
            dst_offset += entry->dstArrayElement / 4;
         else
            dst_offset += binding_layout->size * entry->dstArrayElement / 4;

         dst_stride = binding_layout->size / 4;
         break;
      }

      templ->entry[i] = (struct radv_descriptor_update_template_entry){
         .descriptor_type = entry->descriptorType,
         .descriptor_count = entry->descriptorCount,
         .src_offset = entry->offset,
         .src_stride = entry->stride,
         .dst_offset = dst_offset,
         .dst_stride = dst_stride,
         .buffer_offset = buffer_offset,
         .has_sampler = !binding_layout->immutable_samplers_offset,
         .sampler_offset = radv_combined_image_descriptor_sampler_offset(binding_layout),
         .immutable_samplers = immutable_samplers};
   }

   *pDescriptorUpdateTemplate = radv_descriptor_update_template_to_handle(templ);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyDescriptorUpdateTemplate(VkDevice _device,
                                     VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_descriptor_update_template, templ, descriptorUpdateTemplate);

   if (!templ)
      return;

   vk_object_base_finish(&templ->base);
   vk_free2(&device->vk.alloc, pAllocator, templ);
}

static ALWAYS_INLINE void
radv_update_descriptor_set_with_template_impl(struct radv_device *device,
                                              struct radv_cmd_buffer *cmd_buffer,
                                              struct radv_descriptor_set *set,
                                              VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                              const void *pData)
{
   RADV_FROM_HANDLE(radv_descriptor_update_template, templ, descriptorUpdateTemplate);
   uint32_t i;

   for (i = 0; i < templ->entry_count; ++i) {
      struct radeon_winsys_bo **buffer_list = set->descriptors + templ->entry[i].buffer_offset;
      uint32_t *pDst = set->header.mapped_ptr + templ->entry[i].dst_offset;
      const uint8_t *pSrc = ((const uint8_t *)pData) + templ->entry[i].src_offset;
      uint32_t j;

      if (templ->entry[i].descriptor_type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         memcpy((uint8_t *)pDst, pSrc, templ->entry[i].descriptor_count);
         continue;
      }

      for (j = 0; j < templ->entry[i].descriptor_count; ++j) {
         switch (templ->entry[i].descriptor_type) {
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            const unsigned idx = templ->entry[i].dst_offset + j;
            assert(!(set->header.layout->flags &
                     VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
            write_dynamic_buffer_descriptor(device, set->header.dynamic_descriptors + idx,
                                            buffer_list, (struct VkDescriptorBufferInfo *)pSrc);
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            write_buffer_descriptor(device, cmd_buffer, pDst, buffer_list,
                                    (struct VkDescriptorBufferInfo *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            write_texel_buffer_descriptor(device, cmd_buffer, pDst, buffer_list,
                                          *(VkBufferView *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            write_image_descriptor(device, cmd_buffer, 32, pDst, buffer_list,
                                   templ->entry[i].descriptor_type,
                                   (struct VkDescriptorImageInfo *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            write_image_descriptor(device, cmd_buffer, 64, pDst, buffer_list,
                                   templ->entry[i].descriptor_type,
                                   (struct VkDescriptorImageInfo *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            write_combined_image_sampler_descriptor(
               device, cmd_buffer, templ->entry[i].sampler_offset, pDst, buffer_list,
               templ->entry[i].descriptor_type, (struct VkDescriptorImageInfo *)pSrc,
               templ->entry[i].has_sampler);
            if (cmd_buffer && templ->entry[i].immutable_samplers) {
               memcpy((char *)pDst + templ->entry[i].sampler_offset,
                      templ->entry[i].immutable_samplers + 4 * j, 16);
            }
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            if (templ->entry[i].has_sampler) {
               const VkDescriptorImageInfo *pImageInfo = (struct VkDescriptorImageInfo *)pSrc;
               write_sampler_descriptor(pDst, pImageInfo->sampler);
            }
            else if (cmd_buffer && templ->entry[i].immutable_samplers)
               memcpy(pDst, templ->entry[i].immutable_samplers + 4 * j, 16);
            break;
         case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            write_accel_struct(pDst, *(const VkAccelerationStructureKHR *)pSrc);
            break;
         default:
            break;
         }
         pSrc += templ->entry[i].src_stride;
         pDst += templ->entry[i].dst_stride;
         ++buffer_list;
      }
   }
}

void
radv_cmd_update_descriptor_set_with_template(struct radv_device *device,
                                             struct radv_cmd_buffer *cmd_buffer,
                                             struct radv_descriptor_set *set,
                                             VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                             const void *pData)
{
   /* Assume cmd_buffer != NULL to optimize out cmd_buffer checks in generic code above. */
   assume(cmd_buffer != NULL);
   radv_update_descriptor_set_with_template_impl(device, cmd_buffer, set, descriptorUpdateTemplate, pData);
}

VKAPI_ATTR void VKAPI_CALL
radv_UpdateDescriptorSetWithTemplate(VkDevice _device, VkDescriptorSet descriptorSet,
                                     VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                     const void *pData)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_descriptor_set, set, descriptorSet);

   radv_update_descriptor_set_with_template_impl(device, NULL, set, descriptorUpdateTemplate, pData);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDescriptorSetLayoutHostMappingInfoVALVE(
   VkDevice _device, const VkDescriptorSetBindingReferenceVALVE *pBindingReference,
   VkDescriptorSetLayoutHostMappingInfoVALVE *pHostMapping)
{
   struct radv_descriptor_set_layout *set_layout =
      radv_descriptor_set_layout_from_handle(pBindingReference->descriptorSetLayout);

   const struct radv_descriptor_set_binding_layout *binding_layout =
      set_layout->binding + pBindingReference->binding;

   pHostMapping->descriptorOffset = binding_layout->offset;
   pHostMapping->descriptorSize = binding_layout->size;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDescriptorSetHostMappingVALVE(VkDevice _device, VkDescriptorSet descriptorSet,
                                      void **ppData)
{
   RADV_FROM_HANDLE(radv_descriptor_set, set, descriptorSet);
   *ppData = set->header.mapped_ptr;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateSamplerYcbcrConversion(VkDevice _device,
                                  const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkSamplerYcbcrConversion *pYcbcrConversion)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_sampler_ycbcr_conversion *conversion = NULL;

   conversion = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*conversion), 8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (conversion == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &conversion->base, VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION);

   conversion->state.format = pCreateInfo->format;
   conversion->state.ycbcr_model = pCreateInfo->ycbcrModel;
   conversion->state.ycbcr_range = pCreateInfo->ycbcrRange;
   conversion->state.components = pCreateInfo->components;
   conversion->state.chroma_offsets[0] = pCreateInfo->xChromaOffset;
   conversion->state.chroma_offsets[1] = pCreateInfo->yChromaOffset;
   conversion->state.chroma_filter = pCreateInfo->chromaFilter;

   *pYcbcrConversion = radv_sampler_ycbcr_conversion_to_handle(conversion);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroySamplerYcbcrConversion(VkDevice _device, VkSamplerYcbcrConversion ycbcrConversion,
                                   const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_sampler_ycbcr_conversion, ycbcr_conversion, ycbcrConversion);

   if (!ycbcr_conversion)
      return;

   vk_object_base_finish(&ycbcr_conversion->base);
   vk_free2(&device->vk.alloc, pAllocator, ycbcr_conversion);
}
