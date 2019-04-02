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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "tu_private.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "util/mesa-sha1.h"
#include "vk_util.h"

static int
binding_compare(const void *av, const void *bv)
{
   const VkDescriptorSetLayoutBinding *a =
      (const VkDescriptorSetLayoutBinding *) av;
   const VkDescriptorSetLayoutBinding *b =
      (const VkDescriptorSetLayoutBinding *) bv;

   return (a->binding < b->binding) ? -1 : (a->binding > b->binding) ? 1 : 0;
}

static VkDescriptorSetLayoutBinding *
create_sorted_bindings(const VkDescriptorSetLayoutBinding *bindings,
                       unsigned count)
{
   VkDescriptorSetLayoutBinding *sorted_bindings =
      malloc(count * sizeof(VkDescriptorSetLayoutBinding));
   if (!sorted_bindings)
      return NULL;

   memcpy(sorted_bindings, bindings,
          count * sizeof(VkDescriptorSetLayoutBinding));

   qsort(sorted_bindings, count, sizeof(VkDescriptorSetLayoutBinding),
         binding_compare);

   return sorted_bindings;
}

VkResult
tu_CreateDescriptorSetLayout(
   VkDevice _device,
   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDescriptorSetLayout *pSetLayout)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_descriptor_set_layout *set_layout;

   assert(pCreateInfo->sType ==
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
   const VkDescriptorSetLayoutBindingFlagsCreateInfoEXT *variable_flags =
      vk_find_struct_const(
         pCreateInfo->pNext,
         DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT);

   uint32_t max_binding = 0;
   uint32_t immutable_sampler_count = 0;
   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      max_binding = MAX2(max_binding, pCreateInfo->pBindings[j].binding);
      if (pCreateInfo->pBindings[j].pImmutableSamplers)
         immutable_sampler_count += pCreateInfo->pBindings[j].descriptorCount;
   }

   uint32_t samplers_offset =
      sizeof(struct tu_descriptor_set_layout) +
      (max_binding + 1) * sizeof(set_layout->binding[0]);
   size_t size =
      samplers_offset + immutable_sampler_count * 4 * sizeof(uint32_t);

   set_layout = vk_alloc2(&device->alloc, pAllocator, size, 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!set_layout)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   set_layout->flags = pCreateInfo->flags;

   /* We just allocate all the samplers at the end of the struct */
   uint32_t *samplers = (uint32_t *) &set_layout->binding[max_binding + 1];
   (void) samplers; /* TODO: Use me */

   VkDescriptorSetLayoutBinding *bindings = create_sorted_bindings(
      pCreateInfo->pBindings, pCreateInfo->bindingCount);
   if (!bindings) {
      vk_free2(&device->alloc, pAllocator, set_layout);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   set_layout->binding_count = max_binding + 1;
   set_layout->shader_stages = 0;
   set_layout->dynamic_shader_stages = 0;
   set_layout->has_immutable_samplers = false;
   set_layout->size = 0;

   memset(set_layout->binding, 0,
          size - sizeof(struct tu_descriptor_set_layout));

   uint32_t buffer_count = 0;
   uint32_t dynamic_offset_count = 0;

   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + j;
      uint32_t b = binding->binding;
      uint32_t alignment;
      unsigned binding_buffer_count = 0;

      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         assert(!(pCreateInfo->flags &
                  VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
         set_layout->binding[b].dynamic_offset_count = 1;
         set_layout->dynamic_shader_stages |= binding->stageFlags;
         set_layout->binding[b].size = 0;
         binding_buffer_count = 1;
         alignment = 1;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         set_layout->binding[b].size = 16;
         binding_buffer_count = 1;
         alignment = 16;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         /* main descriptor + fmask descriptor */
         set_layout->binding[b].size = 64;
         binding_buffer_count = 1;
         alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         /* main descriptor + fmask descriptor + sampler */
         set_layout->binding[b].size = 96;
         binding_buffer_count = 1;
         alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         set_layout->binding[b].size = 16;
         alignment = 16;
         break;
      default:
         unreachable("unknown descriptor type\n");
         break;
      }

      set_layout->size = align(set_layout->size, alignment);
      set_layout->binding[b].type = binding->descriptorType;
      set_layout->binding[b].array_size = binding->descriptorCount;
      set_layout->binding[b].offset = set_layout->size;
      set_layout->binding[b].buffer_offset = buffer_count;
      set_layout->binding[b].dynamic_offset_offset = dynamic_offset_count;

      if (variable_flags && binding->binding < variable_flags->bindingCount &&
          (variable_flags->pBindingFlags[binding->binding] &
           VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)) {
         assert(!binding->pImmutableSamplers); /* Terribly ill defined  how
                                                  many samplers are valid */
         assert(binding->binding == max_binding);

         set_layout->has_variable_descriptors = true;
      }

      if (binding->pImmutableSamplers) {
         set_layout->binding[b].immutable_samplers_offset = samplers_offset;
         set_layout->has_immutable_samplers = true;
      }

      set_layout->size +=
         binding->descriptorCount * set_layout->binding[b].size;
      buffer_count += binding->descriptorCount * binding_buffer_count;
      dynamic_offset_count += binding->descriptorCount *
                              set_layout->binding[b].dynamic_offset_count;
      set_layout->shader_stages |= binding->stageFlags;
   }

   free(bindings);

   set_layout->buffer_count = buffer_count;
   set_layout->dynamic_offset_count = dynamic_offset_count;

   *pSetLayout = tu_descriptor_set_layout_to_handle(set_layout);

   return VK_SUCCESS;
}

void
tu_DestroyDescriptorSetLayout(VkDevice _device,
                              VkDescriptorSetLayout _set_layout,
                              const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_set_layout, set_layout, _set_layout);

   if (!set_layout)
      return;

   vk_free2(&device->alloc, pAllocator, set_layout);
}

void
tu_GetDescriptorSetLayoutSupport(
   VkDevice device,
   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   VkDescriptorSetLayoutSupport *pSupport)
{
   VkDescriptorSetLayoutBinding *bindings = create_sorted_bindings(
      pCreateInfo->pBindings, pCreateInfo->bindingCount);
   if (!bindings) {
      pSupport->supported = false;
      return;
   }

   const VkDescriptorSetLayoutBindingFlagsCreateInfoEXT *variable_flags =
      vk_find_struct_const(
         pCreateInfo->pNext,
         DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT);
   VkDescriptorSetVariableDescriptorCountLayoutSupportEXT *variable_count =
      vk_find_struct(
         (void *) pCreateInfo->pNext,
         DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT_EXT);
   if (variable_count) {
      variable_count->maxVariableDescriptorCount = 0;
   }

   bool supported = true;
   uint64_t size = 0;
   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + i;

      uint64_t descriptor_size = 0;
      uint64_t descriptor_alignment = 1;
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
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         descriptor_size = 64;
         descriptor_alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         descriptor_size = 96;
         descriptor_alignment = 32;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         descriptor_size = 16;
         descriptor_alignment = 16;
         break;
      default:
         unreachable("unknown descriptor type\n");
         break;
      }

      if (size && !align_u64(size, descriptor_alignment)) {
         supported = false;
      }
      size = align_u64(size, descriptor_alignment);

      uint64_t max_count = UINT64_MAX;
      if (descriptor_size)
         max_count = (UINT64_MAX - size) / descriptor_size;

      if (max_count < binding->descriptorCount) {
         supported = false;
      }
      if (variable_flags && binding->binding < variable_flags->bindingCount &&
          variable_count &&
          (variable_flags->pBindingFlags[binding->binding] &
           VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)) {
         variable_count->maxVariableDescriptorCount =
            MIN2(UINT32_MAX, max_count);
      }
      size += binding->descriptorCount * descriptor_size;
   }

   free(bindings);

   pSupport->supported = supported;
}

/*
 * Pipeline layouts.  These have nothing to do with the pipeline.  They are
 * just multiple descriptor set layouts pasted together.
 */

VkResult
tu_CreatePipelineLayout(VkDevice _device,
                        const VkPipelineLayoutCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkPipelineLayout *pPipelineLayout)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_pipeline_layout *layout;
   struct mesa_sha1 ctx;

   assert(pCreateInfo->sType ==
          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

   layout = vk_alloc2(&device->alloc, pAllocator, sizeof(*layout), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (layout == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   layout->num_sets = pCreateInfo->setLayoutCount;

   unsigned dynamic_offset_count = 0;

   _mesa_sha1_init(&ctx);
   for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
      TU_FROM_HANDLE(tu_descriptor_set_layout, set_layout,
                     pCreateInfo->pSetLayouts[set]);
      layout->set[set].layout = set_layout;

      layout->set[set].dynamic_offset_start = dynamic_offset_count;
      for (uint32_t b = 0; b < set_layout->binding_count; b++) {
         dynamic_offset_count += set_layout->binding[b].array_size *
                                 set_layout->binding[b].dynamic_offset_count;
         if (set_layout->binding[b].immutable_samplers_offset)
            _mesa_sha1_update(
               &ctx,
               tu_immutable_samplers(set_layout, set_layout->binding + b),
               set_layout->binding[b].array_size * 4 * sizeof(uint32_t));
      }
      _mesa_sha1_update(
         &ctx, set_layout->binding,
         sizeof(set_layout->binding[0]) * set_layout->binding_count);
   }

   layout->dynamic_offset_count = dynamic_offset_count;
   layout->push_constant_size = 0;

   for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
      const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + i;
      layout->push_constant_size =
         MAX2(layout->push_constant_size, range->offset + range->size);
   }

   layout->push_constant_size = align(layout->push_constant_size, 16);
   _mesa_sha1_update(&ctx, &layout->push_constant_size,
                     sizeof(layout->push_constant_size));
   _mesa_sha1_final(&ctx, layout->sha1);
   *pPipelineLayout = tu_pipeline_layout_to_handle(layout);

   return VK_SUCCESS;
}

void
tu_DestroyPipelineLayout(VkDevice _device,
                         VkPipelineLayout _pipelineLayout,
                         const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_pipeline_layout, pipeline_layout, _pipelineLayout);

   if (!pipeline_layout)
      return;
   vk_free2(&device->alloc, pAllocator, pipeline_layout);
}

#define EMPTY 1

VkResult
tu_CreateDescriptorPool(VkDevice _device,
                        const VkDescriptorPoolCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkDescriptorPool *pDescriptorPool)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   tu_use_args(device);
   tu_stub();
   return VK_SUCCESS;
}

void
tu_DestroyDescriptorPool(VkDevice _device,
                         VkDescriptorPool _pool,
                         const VkAllocationCallbacks *pAllocator)
{
}

VkResult
tu_ResetDescriptorPool(VkDevice _device,
                       VkDescriptorPool descriptorPool,
                       VkDescriptorPoolResetFlags flags)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_pool, pool, descriptorPool);

   tu_use_args(device, pool);
   tu_stub();
   return VK_SUCCESS;
}

VkResult
tu_AllocateDescriptorSets(VkDevice _device,
                          const VkDescriptorSetAllocateInfo *pAllocateInfo,
                          VkDescriptorSet *pDescriptorSets)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_pool, pool, pAllocateInfo->descriptorPool);

   tu_use_args(device, pool);
   tu_stub();
   return VK_SUCCESS;
}

VkResult
tu_FreeDescriptorSets(VkDevice _device,
                      VkDescriptorPool descriptorPool,
                      uint32_t count,
                      const VkDescriptorSet *pDescriptorSets)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_pool, pool, descriptorPool);

   tu_use_args(device, pool);
   tu_stub();
   return VK_SUCCESS;
}

void
tu_update_descriptor_sets(struct tu_device *device,
                          struct tu_cmd_buffer *cmd_buffer,
                          VkDescriptorSet dstSetOverride,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies)
{
}

void
tu_UpdateDescriptorSets(VkDevice _device,
                        uint32_t descriptorWriteCount,
                        const VkWriteDescriptorSet *pDescriptorWrites,
                        uint32_t descriptorCopyCount,
                        const VkCopyDescriptorSet *pDescriptorCopies)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   tu_update_descriptor_sets(device, NULL, VK_NULL_HANDLE,
                             descriptorWriteCount, pDescriptorWrites,
                             descriptorCopyCount, pDescriptorCopies);
}

VkResult
tu_CreateDescriptorUpdateTemplate(
   VkDevice _device,
   const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_set_layout, set_layout,
                  pCreateInfo->descriptorSetLayout);
   const uint32_t entry_count = pCreateInfo->descriptorUpdateEntryCount;
   const size_t size =
      sizeof(struct tu_descriptor_update_template) +
      sizeof(struct tu_descriptor_update_template_entry) * entry_count;
   struct tu_descriptor_update_template *templ;

   templ = vk_alloc2(&device->alloc, pAllocator, size, 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!templ)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   *pDescriptorUpdateTemplate =
      tu_descriptor_update_template_to_handle(templ);

   tu_use_args(set_layout);
   tu_stub();
   return VK_SUCCESS;
}

void
tu_DestroyDescriptorUpdateTemplate(
   VkDevice _device,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_update_template, templ,
                  descriptorUpdateTemplate);

   if (!templ)
      return;

   vk_free2(&device->alloc, pAllocator, templ);
}

void
tu_update_descriptor_set_with_template(
   struct tu_device *device,
   struct tu_cmd_buffer *cmd_buffer,
   struct tu_descriptor_set *set,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData)
{
   TU_FROM_HANDLE(tu_descriptor_update_template, templ,
                  descriptorUpdateTemplate);
   tu_use_args(templ);
}

void
tu_UpdateDescriptorSetWithTemplate(
   VkDevice _device,
   VkDescriptorSet descriptorSet,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_set, set, descriptorSet);

   tu_update_descriptor_set_with_template(device, NULL, set,
                                          descriptorUpdateTemplate, pData);
}

VkResult
tu_CreateSamplerYcbcrConversion(
   VkDevice device,
   const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkSamplerYcbcrConversion *pYcbcrConversion)
{
   *pYcbcrConversion = VK_NULL_HANDLE;
   return VK_SUCCESS;
}

void
tu_DestroySamplerYcbcrConversion(VkDevice device,
                                 VkSamplerYcbcrConversion ycbcrConversion,
                                 const VkAllocationCallbacks *pAllocator)
{
   /* Do nothing. */
}
