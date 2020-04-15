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

/**
 * @file
 *
 * We use the bindless descriptor model, which maps fairly closely to how
 * Vulkan descriptor sets work. The two exceptions are input attachments and
 * dynamic descriptors, which have to be patched when recording command
 * buffers. We reserve an extra descriptor set for these. This descriptor set
 * contains all the input attachments in the pipeline, in order, and then all
 * the dynamic descriptors. The dynamic descriptors are stored in the CPU-side
 * datastructure for each tu_descriptor_set, and then combined into one big
 * descriptor set at CmdBindDescriptors time/draw time.
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

static uint32_t
descriptor_size(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      /* These are remapped to the special driver-managed descriptor set,
       * hence they don't take up any space in the original descriptor set:
       */
      return 0;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      /* We make offsets and sizes all 16 dwords, to match how the hardware
       * interprets indices passed to sample/load/store instructions in
       * multiples of 16 dwords.  This means that "normal" descriptors are all
       * of size 16, with padding for smaller descriptors like uniform storage
       * descriptors which are less than 16 dwords. However combined images
       * and samplers are actually two descriptors, so they have size 2.
       */
      return A6XX_TEX_CONST_DWORDS * 4 * 2;
   default:
      return A6XX_TEX_CONST_DWORDS * 4;
   }
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
      if ((pCreateInfo->pBindings[j].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           pCreateInfo->pBindings[j].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) &&
           pCreateInfo->pBindings[j].pImmutableSamplers) {
         immutable_sampler_count += pCreateInfo->pBindings[j].descriptorCount;
      }
   }

   uint32_t samplers_offset = sizeof(struct tu_descriptor_set_layout) +
      (max_binding + 1) * sizeof(set_layout->binding[0]);
   uint32_t size = samplers_offset + immutable_sampler_count * A6XX_TEX_SAMP_DWORDS * 4;

   set_layout = vk_alloc2(&device->alloc, pAllocator, size, 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!set_layout)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   set_layout->flags = pCreateInfo->flags;

   /* We just allocate all the immutable samplers at the end of the struct */
   struct tu_sampler *samplers = (void*) &set_layout->binding[max_binding + 1];

   VkDescriptorSetLayoutBinding *bindings = create_sorted_bindings(
      pCreateInfo->pBindings, pCreateInfo->bindingCount);
   if (!bindings) {
      vk_free2(&device->alloc, pAllocator, set_layout);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   set_layout->binding_count = max_binding + 1;
   set_layout->shader_stages = 0;
   set_layout->has_immutable_samplers = false;
   set_layout->size = 0;
   set_layout->dynamic_ubo = 0;

   memset(set_layout->binding, 0,
          size - sizeof(struct tu_descriptor_set_layout));

   uint32_t dynamic_offset_count = 0;
   uint32_t input_attachment_count = 0;
   uint32_t buffer_count = 0;

   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + j;
      uint32_t b = binding->binding;

      set_layout->binding[b].type = binding->descriptorType;
      set_layout->binding[b].array_size = binding->descriptorCount;
      set_layout->binding[b].offset = set_layout->size;
      set_layout->binding[b].buffer_offset = buffer_count;
      set_layout->binding[b].dynamic_offset_offset = dynamic_offset_count;
      set_layout->binding[b].input_attachment_offset = input_attachment_count;
      set_layout->binding[b].size = descriptor_size(binding->descriptorType);
      set_layout->binding[b].shader_stages = binding->stageFlags;

      if (variable_flags && binding->binding < variable_flags->bindingCount &&
          (variable_flags->pBindingFlags[binding->binding] &
           VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)) {
         assert(!binding->pImmutableSamplers); /* Terribly ill defined  how
                                                  many samplers are valid */
         assert(binding->binding == max_binding);

         set_layout->has_variable_descriptors = true;
      }

      if ((binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) &&
          binding->pImmutableSamplers) {
         set_layout->binding[b].immutable_samplers_offset = samplers_offset;
         set_layout->has_immutable_samplers = true;

         for (uint32_t i = 0; i < binding->descriptorCount; i++)
            samplers[i] = *tu_sampler_from_handle(binding->pImmutableSamplers[i]);

         samplers += binding->descriptorCount;
         samplers_offset += sizeof(struct tu_sampler) * binding->descriptorCount;
      }

      set_layout->size +=
         binding->descriptorCount * set_layout->binding[b].size;
      if (binding->descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER &&
          binding->descriptorType != VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT)
         buffer_count += binding->descriptorCount;
      if (binding->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
          binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
         if (binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            STATIC_ASSERT(MAX_DYNAMIC_BUFFERS <= 8 * sizeof(set_layout->dynamic_ubo));
            set_layout->dynamic_ubo |=
               ((1u << binding->descriptorCount) - 1) << dynamic_offset_count;
         }

         dynamic_offset_count += binding->descriptorCount;
      }
      if (binding->descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
         input_attachment_count += binding->descriptorCount;
      set_layout->shader_stages |= binding->stageFlags;
   }

   free(bindings);

   set_layout->dynamic_offset_count = dynamic_offset_count;
   set_layout->input_attachment_count = input_attachment_count;
   set_layout->buffer_count = buffer_count;

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

      uint64_t descriptor_sz = descriptor_size(binding->descriptorType);
      uint64_t descriptor_alignment = 8;

      if (size && !align_u64(size, descriptor_alignment)) {
         supported = false;
      }
      size = align_u64(size, descriptor_alignment);

      uint64_t max_count = UINT64_MAX;
      if (descriptor_sz)
         max_count = (UINT64_MAX - size) / descriptor_sz;

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
      size += binding->descriptorCount * descriptor_sz;
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
   layout->input_attachment_count = 0;
   layout->dynamic_offset_count = 0;

   unsigned dynamic_offset_count = 0, input_attachment_count = 0;

   _mesa_sha1_init(&ctx);
   for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
      TU_FROM_HANDLE(tu_descriptor_set_layout, set_layout,
                     pCreateInfo->pSetLayouts[set]);
      layout->set[set].layout = set_layout;
      layout->set[set].dynamic_offset_start = dynamic_offset_count;
      layout->set[set].input_attachment_start = input_attachment_count;
      dynamic_offset_count += set_layout->dynamic_offset_count;
      input_attachment_count += set_layout->input_attachment_count;

      for (uint32_t b = 0; b < set_layout->binding_count; b++) {
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
   layout->input_attachment_count = input_attachment_count;
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

static VkResult
tu_descriptor_set_create(struct tu_device *device,
            struct tu_descriptor_pool *pool,
            const struct tu_descriptor_set_layout *layout,
            const uint32_t *variable_count,
            struct tu_descriptor_set **out_set)
{
   struct tu_descriptor_set *set;
   uint32_t buffer_count = layout->buffer_count;
   if (variable_count) {
      unsigned stride = 1;
      if (layout->binding[layout->binding_count - 1].type == VK_DESCRIPTOR_TYPE_SAMPLER ||
          layout->binding[layout->binding_count - 1].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT)
         stride = 0;
      buffer_count = layout->binding[layout->binding_count - 1].buffer_offset +
                     *variable_count * stride;
   }
   unsigned dynamic_offset = sizeof(struct tu_descriptor_set) +
      sizeof(struct tu_bo *) * buffer_count;
   unsigned mem_size = dynamic_offset +
      A6XX_TEX_CONST_DWORDS * 4 * (layout->dynamic_offset_count +
                                   layout->input_attachment_count);;

   if (pool->host_memory_base) {
      if (pool->host_memory_end - pool->host_memory_ptr < mem_size)
         return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);

      set = (struct tu_descriptor_set*)pool->host_memory_ptr;
      pool->host_memory_ptr += mem_size;
   } else {
      set = vk_alloc2(&device->alloc, NULL, mem_size, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      if (!set)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   memset(set, 0, mem_size);

   if (layout->dynamic_offset_count + layout->input_attachment_count > 0) {
      set->dynamic_descriptors = (uint32_t *)((uint8_t*)set + dynamic_offset);
   }

   set->layout = layout;
   set->pool = pool;
   uint32_t layout_size = layout->size;
   if (variable_count) {
      assert(layout->has_variable_descriptors);
      uint32_t stride = layout->binding[layout->binding_count - 1].size;
      layout_size = layout->binding[layout->binding_count - 1].offset +
                    *variable_count * stride;
   }

   if (layout_size) {
      set->size = layout_size;

      if (!pool->host_memory_base && pool->entry_count == pool->max_entry_count) {
         vk_free2(&device->alloc, NULL, set);
         return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);
      }

      /* try to allocate linearly first, so that we don't spend
       * time looking for gaps if the app only allocates &
       * resets via the pool. */
      if (pool->current_offset + layout_size <= pool->size) {
         set->mapped_ptr = (uint32_t*)(pool->bo.map + pool->current_offset);
         set->va = pool->bo.iova + pool->current_offset;
         if (!pool->host_memory_base) {
            pool->entries[pool->entry_count].offset = pool->current_offset;
            pool->entries[pool->entry_count].size = layout_size;
            pool->entries[pool->entry_count].set = set;
            pool->entry_count++;
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
            vk_free2(&device->alloc, NULL, set);
            return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);
         }

         set->mapped_ptr = (uint32_t*)(pool->bo.map + offset);
         set->va = pool->bo.iova + offset;
         memmove(&pool->entries[index + 1], &pool->entries[index],
            sizeof(pool->entries[0]) * (pool->entry_count - index));
         pool->entries[index].offset = offset;
         pool->entries[index].size = layout_size;
         pool->entries[index].set = set;
         pool->entry_count++;
      } else
         return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);
   }

   if (layout->has_immutable_samplers) {
      for (unsigned i = 0; i < layout->binding_count; ++i) {
         if (!layout->binding[i].immutable_samplers_offset)
            continue;

         unsigned offset = layout->binding[i].offset / 4;
         if (layout->binding[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            offset += A6XX_TEX_CONST_DWORDS;

         const struct tu_sampler *samplers =
            (const struct tu_sampler *)((const char *)layout +
                               layout->binding[i].immutable_samplers_offset);
         for (unsigned j = 0; j < layout->binding[i].array_size; ++j) {
            memcpy(set->mapped_ptr + offset, samplers + j,
                   sizeof(struct tu_sampler));
            offset += layout->binding[i].size / 4;
         }
      }
   }

   *out_set = set;
   return VK_SUCCESS;
}

static void
tu_descriptor_set_destroy(struct tu_device *device,
             struct tu_descriptor_pool *pool,
             struct tu_descriptor_set *set,
             bool free_bo)
{
   assert(!pool->host_memory_base);

   if (free_bo && set->size && !pool->host_memory_base) {
      uint32_t offset = (uint8_t*)set->mapped_ptr - (uint8_t*)pool->bo.map;
      for (int i = 0; i < pool->entry_count; ++i) {
         if (pool->entries[i].offset == offset) {
            memmove(&pool->entries[i], &pool->entries[i+1],
               sizeof(pool->entries[i]) * (pool->entry_count - i - 1));
            --pool->entry_count;
            break;
         }
      }
   }
   vk_free2(&device->alloc, NULL, set);
}

VkResult
tu_CreateDescriptorPool(VkDevice _device,
                        const VkDescriptorPoolCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkDescriptorPool *pDescriptorPool)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_descriptor_pool *pool;
   uint64_t size = sizeof(struct tu_descriptor_pool);
   uint64_t bo_size = 0, bo_count = 0, dynamic_count = 0;

   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      if (pCreateInfo->pPoolSizes[i].type != VK_DESCRIPTOR_TYPE_SAMPLER)
         bo_count += pCreateInfo->pPoolSizes[i].descriptorCount;

      switch(pCreateInfo->pPoolSizes[i].type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         dynamic_count += pCreateInfo->pPoolSizes[i].descriptorCount;
      default:
         break;
      }

      bo_size += descriptor_size(pCreateInfo->pPoolSizes[i].type) *
                           pCreateInfo->pPoolSizes[i].descriptorCount;
   }

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      uint64_t host_size = pCreateInfo->maxSets * sizeof(struct tu_descriptor_set);
      host_size += sizeof(struct tu_bo*) * bo_count;
      host_size += A6XX_TEX_CONST_DWORDS * 4 * dynamic_count;
      size += host_size;
   } else {
      size += sizeof(struct tu_descriptor_pool_entry) * pCreateInfo->maxSets;
   }

   pool = vk_alloc2(&device->alloc, pAllocator, size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(pool, 0, sizeof(*pool));

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      pool->host_memory_base = (uint8_t*)pool + sizeof(struct tu_descriptor_pool);
      pool->host_memory_ptr = pool->host_memory_base;
      pool->host_memory_end = (uint8_t*)pool + size;
   }

   if (bo_size) {
      VkResult ret;

      ret = tu_bo_init_new(device, &pool->bo, bo_size);
      assert(ret == VK_SUCCESS);

      ret = tu_bo_map(device, &pool->bo);
      assert(ret == VK_SUCCESS);
   }
   pool->size = bo_size;
   pool->max_entry_count = pCreateInfo->maxSets;

   *pDescriptorPool = tu_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

void
tu_DestroyDescriptorPool(VkDevice _device,
                         VkDescriptorPool _pool,
                         const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_pool, pool, _pool);

   if (!pool)
      return;

   if (!pool->host_memory_base) {
      for(int i = 0; i < pool->entry_count; ++i) {
         tu_descriptor_set_destroy(device, pool, pool->entries[i].set, false);
      }
   }

   if (pool->size)
      tu_bo_finish(device, &pool->bo);
   vk_free2(&device->alloc, pAllocator, pool);
}

VkResult
tu_ResetDescriptorPool(VkDevice _device,
                       VkDescriptorPool descriptorPool,
                       VkDescriptorPoolResetFlags flags)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_pool, pool, descriptorPool);

   if (!pool->host_memory_base) {
      for(int i = 0; i < pool->entry_count; ++i) {
         tu_descriptor_set_destroy(device, pool, pool->entries[i].set, false);
      }
      pool->entry_count = 0;
   }

   pool->current_offset = 0;
   pool->host_memory_ptr = pool->host_memory_base;

   return VK_SUCCESS;
}

VkResult
tu_AllocateDescriptorSets(VkDevice _device,
                          const VkDescriptorSetAllocateInfo *pAllocateInfo,
                          VkDescriptorSet *pDescriptorSets)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_pool, pool, pAllocateInfo->descriptorPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;
   struct tu_descriptor_set *set = NULL;

   const VkDescriptorSetVariableDescriptorCountAllocateInfoEXT *variable_counts =
      vk_find_struct_const(pAllocateInfo->pNext, DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT);
   const uint32_t zero = 0;

   /* allocate a set of buffers for each shader to contain descriptors */
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      TU_FROM_HANDLE(tu_descriptor_set_layout, layout,
             pAllocateInfo->pSetLayouts[i]);

      const uint32_t *variable_count = NULL;
      if (variable_counts) {
         if (i < variable_counts->descriptorSetCount)
            variable_count = variable_counts->pDescriptorCounts + i;
         else
            variable_count = &zero;
      }

      assert(!(layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));

      result = tu_descriptor_set_create(device, pool, layout, variable_count, &set);
      if (result != VK_SUCCESS)
         break;

      pDescriptorSets[i] = tu_descriptor_set_to_handle(set);
   }

   if (result != VK_SUCCESS) {
      tu_FreeDescriptorSets(_device, pAllocateInfo->descriptorPool,
               i, pDescriptorSets);
      for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
         pDescriptorSets[i] = VK_NULL_HANDLE;
      }
   }
   return result;
}

VkResult
tu_FreeDescriptorSets(VkDevice _device,
                      VkDescriptorPool descriptorPool,
                      uint32_t count,
                      const VkDescriptorSet *pDescriptorSets)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_descriptor_pool, pool, descriptorPool);

   for (uint32_t i = 0; i < count; i++) {
      TU_FROM_HANDLE(tu_descriptor_set, set, pDescriptorSets[i]);

      if (set && !pool->host_memory_base)
         tu_descriptor_set_destroy(device, pool, set, true);
   }
   return VK_SUCCESS;
}

static void write_texel_buffer_descriptor(struct tu_device *device,
                                          struct tu_cmd_buffer *cmd_buffer,
                                          unsigned *dst,
                                          struct tu_bo **buffer_list,
                                          const VkBufferView buffer_view)
{
   TU_FROM_HANDLE(tu_buffer_view, view, buffer_view);

   memcpy(dst, view->descriptor, sizeof(view->descriptor));

   if (cmd_buffer)
      tu_bo_list_add(&cmd_buffer->bo_list, view->buffer->bo, MSM_SUBMIT_BO_READ);
   else
      *buffer_list = view->buffer->bo;
}

static uint32_t get_range(struct tu_buffer *buf, VkDeviceSize offset,
                          VkDeviceSize range)
{
   if (range == VK_WHOLE_SIZE) {
      return buf->size - offset;
   } else {
      return range;
   }
}

static void write_buffer_descriptor(struct tu_device *device,
                                    struct tu_cmd_buffer *cmd_buffer,
                                    unsigned *dst,
                                    struct tu_bo **buffer_list,
                                    const VkDescriptorBufferInfo *buffer_info)
{
   TU_FROM_HANDLE(tu_buffer, buffer, buffer_info->buffer);

   uint64_t va = tu_buffer_iova(buffer) + buffer_info->offset;
   uint32_t range = get_range(buffer, buffer_info->offset, buffer_info->range);
   range = ALIGN_POT(range, 4) / 4;
   dst[0] =
      A6XX_IBO_0_TILE_MODE(TILE6_LINEAR) | A6XX_IBO_0_FMT(FMT6_32_UINT);
   dst[1] = range;
   dst[2] =
      A6XX_IBO_2_UNK4 | A6XX_IBO_2_TYPE(A6XX_TEX_1D) | A6XX_IBO_2_UNK31;
   dst[3] = 0;
   dst[4] = A6XX_IBO_4_BASE_LO(va);
   dst[5] = A6XX_IBO_5_BASE_HI(va >> 32);
   for (int i = 6; i < A6XX_TEX_CONST_DWORDS; i++)
      dst[i] = 0;

   if (cmd_buffer)
      tu_bo_list_add(&cmd_buffer->bo_list, buffer->bo, MSM_SUBMIT_BO_READ);
   else
      *buffer_list = buffer->bo;
}

static void write_ubo_descriptor(struct tu_device *device,
                                 struct tu_cmd_buffer *cmd_buffer,
                                 unsigned *dst,
                                 struct tu_bo **buffer_list,
                                 const VkDescriptorBufferInfo *buffer_info)
{
   TU_FROM_HANDLE(tu_buffer, buffer, buffer_info->buffer);

   uint32_t range = get_range(buffer, buffer_info->offset, buffer_info->range);
   /* The HW range is in vec4 units */
   range = ALIGN_POT(range, 16) / 16;
   uint64_t va = tu_buffer_iova(buffer) + buffer_info->offset;
   dst[0] = A6XX_UBO_0_BASE_LO(va);
   dst[1] = A6XX_UBO_1_BASE_HI(va >> 32) | A6XX_UBO_1_SIZE(range);

   if (cmd_buffer)
      tu_bo_list_add(&cmd_buffer->bo_list, buffer->bo, MSM_SUBMIT_BO_READ);
   else
      *buffer_list = buffer->bo;
}

static void
write_image_descriptor(struct tu_device *device,
             struct tu_cmd_buffer *cmd_buffer,
             unsigned *dst,
             struct tu_bo **buffer_list,
             VkDescriptorType descriptor_type,
             const VkDescriptorImageInfo *image_info)
{
   TU_FROM_HANDLE(tu_image_view, iview, image_info->imageView);

   if (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
      memcpy(dst, iview->storage_descriptor, sizeof(iview->storage_descriptor));
   } else {
      memcpy(dst, iview->descriptor, sizeof(iview->descriptor));
   }

   if (cmd_buffer)
      tu_bo_list_add(&cmd_buffer->bo_list, iview->image->bo, MSM_SUBMIT_BO_READ);
   else
      *buffer_list = iview->image->bo;
}

static void
write_combined_image_sampler_descriptor(struct tu_device *device,
               struct tu_cmd_buffer *cmd_buffer,
               unsigned sampler_offset,
               unsigned *dst,
               struct tu_bo **buffer_list,
               VkDescriptorType descriptor_type,
               const VkDescriptorImageInfo *image_info,
               bool has_sampler)
{
   TU_FROM_HANDLE(tu_sampler, sampler, image_info->sampler);

   write_image_descriptor(device, cmd_buffer, dst, buffer_list,
                          descriptor_type, image_info);
   /* copy over sampler state */
   if (has_sampler) {
      memcpy(dst + A6XX_TEX_CONST_DWORDS, sampler, sizeof(*sampler));
   }
}

static void
write_sampler_descriptor(struct tu_device *device,
                         unsigned *dst,
                         const VkDescriptorImageInfo *image_info)
{
   TU_FROM_HANDLE(tu_sampler, sampler, image_info->sampler);

   memcpy(dst, sampler, sizeof(*sampler));
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
   uint32_t i, j;
   for (i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *writeset = &pDescriptorWrites[i];
      TU_FROM_HANDLE(tu_descriptor_set, set,
                       dstSetOverride ? dstSetOverride : writeset->dstSet);
      const struct tu_descriptor_set_binding_layout *binding_layout =
         set->layout->binding + writeset->dstBinding;
      uint32_t *ptr = set->mapped_ptr;
      struct tu_bo **buffer_list = set->buffers;

      ptr += binding_layout->offset / 4;

      ptr += (binding_layout->size / 4) * writeset->dstArrayElement;
      buffer_list += binding_layout->buffer_offset;
      buffer_list += writeset->dstArrayElement;
      for (j = 0; j < writeset->descriptorCount; ++j) {
         switch(writeset->descriptorType) {
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
            assert(!(set->layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
            unsigned idx = writeset->dstArrayElement + j;
            idx += set->layout->input_attachment_count + binding_layout->dynamic_offset_offset;
            write_ubo_descriptor(device, cmd_buffer,
                                 set->dynamic_descriptors + A6XX_TEX_CONST_DWORDS * idx,
                                 buffer_list, writeset->pBufferInfo + j);
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            write_ubo_descriptor(device, cmd_buffer, ptr, buffer_list,
                     writeset->pBufferInfo + j);
            break;
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            assert(!(set->layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
            unsigned idx = writeset->dstArrayElement + j;
            idx += set->layout->input_attachment_count + binding_layout->dynamic_offset_offset;
            write_buffer_descriptor(device, cmd_buffer,
                                    set->dynamic_descriptors + A6XX_TEX_CONST_DWORDS * idx,
                                    buffer_list, writeset->pBufferInfo + j);
            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            write_buffer_descriptor(device, cmd_buffer, ptr, buffer_list,
                     writeset->pBufferInfo + j);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            write_texel_buffer_descriptor(device, cmd_buffer, ptr, buffer_list,
                     writeset->pTexelBufferView[j]);
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            write_image_descriptor(device, cmd_buffer, ptr, buffer_list,
                                   writeset->descriptorType,
                                   writeset->pImageInfo + j);
            break;
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            unsigned idx = writeset->dstArrayElement + j;
            idx += binding_layout->input_attachment_offset;
            write_image_descriptor(device, cmd_buffer,
                                    set->dynamic_descriptors + A6XX_TEX_CONST_DWORDS * idx,
                                    buffer_list, writeset->descriptorType,
                                    writeset->pImageInfo + j);
            break;
         }
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            write_combined_image_sampler_descriptor(device, cmd_buffer,
                                                    A6XX_TEX_CONST_DWORDS * 4,
                                                    ptr, buffer_list,
                                                    writeset->descriptorType,
                                                    writeset->pImageInfo + j,
                                                    !binding_layout->immutable_samplers_offset);
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            write_sampler_descriptor(device, ptr, writeset->pImageInfo + j);
            break;
         default:
            unreachable("unimplemented descriptor type");
            break;
         }
         ptr += binding_layout->size / 4;
         ++buffer_list;
      }
   }

   for (i = 0; i < descriptorCopyCount; i++) {
      const VkCopyDescriptorSet *copyset = &pDescriptorCopies[i];
      TU_FROM_HANDLE(tu_descriptor_set, src_set,
                       copyset->srcSet);
      TU_FROM_HANDLE(tu_descriptor_set, dst_set,
                       copyset->dstSet);
      const struct tu_descriptor_set_binding_layout *src_binding_layout =
         src_set->layout->binding + copyset->srcBinding;
      const struct tu_descriptor_set_binding_layout *dst_binding_layout =
         dst_set->layout->binding + copyset->dstBinding;
      uint32_t *src_ptr = src_set->mapped_ptr;
      uint32_t *dst_ptr = dst_set->mapped_ptr;
      struct tu_bo **src_buffer_list = src_set->buffers;
      struct tu_bo **dst_buffer_list = dst_set->buffers;

      src_ptr += src_binding_layout->offset / 4;
      dst_ptr += dst_binding_layout->offset / 4;

      src_ptr += src_binding_layout->size * copyset->srcArrayElement / 4;
      dst_ptr += dst_binding_layout->size * copyset->dstArrayElement / 4;

      src_buffer_list += src_binding_layout->buffer_offset;
      src_buffer_list += copyset->srcArrayElement;

      dst_buffer_list += dst_binding_layout->buffer_offset;
      dst_buffer_list += copyset->dstArrayElement;

      for (j = 0; j < copyset->descriptorCount; ++j) {
         switch (src_binding_layout->type) {
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            unsigned src_idx = copyset->srcArrayElement + j;
            unsigned dst_idx = copyset->dstArrayElement + j;
            src_idx += src_set->layout->input_attachment_count;
            dst_idx += dst_set->layout->input_attachment_count;
            src_idx += src_binding_layout->dynamic_offset_offset;
            dst_idx += dst_binding_layout->dynamic_offset_offset;

            uint32_t *src_dynamic, *dst_dynamic;
            src_dynamic = src_set->dynamic_descriptors + src_idx * A6XX_TEX_CONST_DWORDS;
            dst_dynamic = dst_set->dynamic_descriptors + dst_idx * A6XX_TEX_CONST_DWORDS;
            memcpy(dst_dynamic, src_dynamic, A6XX_TEX_CONST_DWORDS * 4);
            break;
         }
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            unsigned src_idx = copyset->srcArrayElement + j;
            unsigned dst_idx = copyset->dstArrayElement + j;
            src_idx += src_binding_layout->input_attachment_offset;
            dst_idx += dst_binding_layout->input_attachment_offset;

            uint32_t *src_dynamic, *dst_dynamic;
            src_dynamic = src_set->dynamic_descriptors + src_idx * A6XX_TEX_CONST_DWORDS;
            dst_dynamic = dst_set->dynamic_descriptors + dst_idx * A6XX_TEX_CONST_DWORDS;
            memcpy(dst_dynamic, src_dynamic, A6XX_TEX_CONST_DWORDS * 4);
            break;
         }
         default:
            memcpy(dst_ptr, src_ptr, src_binding_layout->size);
         }

         src_ptr += src_binding_layout->size / 4;
         dst_ptr += dst_binding_layout->size / 4;

         if (src_binding_layout->type != VK_DESCRIPTOR_TYPE_SAMPLER) {
            /* Sampler descriptors don't have a buffer list. */
            dst_buffer_list[j] = src_buffer_list[j];
         }
      }
   }
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

   templ->entry_count = entry_count;

   if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
      TU_FROM_HANDLE(tu_pipeline_layout, pipeline_layout, pCreateInfo->pipelineLayout);

      /* descriptorSetLayout should be ignored for push descriptors
       * and instead it refers to pipelineLayout and set.
       */
      assert(pCreateInfo->set < MAX_SETS);
      set_layout = pipeline_layout->set[pCreateInfo->set].layout;
   }

   for (uint32_t i = 0; i < entry_count; i++) {
      const VkDescriptorUpdateTemplateEntry *entry = &pCreateInfo->pDescriptorUpdateEntries[i];

      const struct tu_descriptor_set_binding_layout *binding_layout =
         set_layout->binding + entry->dstBinding;
      const uint32_t buffer_offset = binding_layout->buffer_offset +
         entry->dstArrayElement;
      uint32_t dst_offset, dst_stride;

      /* dst_offset is an offset into dynamic_descriptors when the descriptor 
       * is dynamic, and an offset into mapped_ptr otherwise.
       */
      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         dst_offset = (set_layout->input_attachment_count +
            binding_layout->dynamic_offset_offset +
            entry->dstArrayElement) * A6XX_TEX_CONST_DWORDS;
         dst_stride = A6XX_TEX_CONST_DWORDS;
         break;
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         dst_offset = (binding_layout->input_attachment_offset +
            entry->dstArrayElement) * A6XX_TEX_CONST_DWORDS;
         dst_stride = A6XX_TEX_CONST_DWORDS;
         break;
      default:
         dst_offset = binding_layout->offset / 4;
         dst_offset += (binding_layout->size * entry->dstArrayElement) / 4;
         dst_stride = binding_layout->size / 4;
      }

      templ->entry[i] = (struct tu_descriptor_update_template_entry) {
         .descriptor_type = entry->descriptorType,
         .descriptor_count = entry->descriptorCount,
         .src_offset = entry->offset,
         .src_stride = entry->stride,
         .dst_offset = dst_offset,
         .dst_stride = dst_stride,
         .buffer_offset = buffer_offset,
         .has_sampler = !binding_layout->immutable_samplers_offset,
      };
   }

   *pDescriptorUpdateTemplate =
      tu_descriptor_update_template_to_handle(templ);

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

   for (uint32_t i = 0; i < templ->entry_count; i++) {
      uint32_t *ptr = set->mapped_ptr;
      const void *src = ((const char *) pData) + templ->entry[i].src_offset;
      struct tu_bo **buffer_list = set->buffers;

      ptr += templ->entry[i].dst_offset;
      buffer_list += templ->entry[i].buffer_offset;
      unsigned dst_offset = templ->entry[i].dst_offset;
      for (unsigned j = 0; j < templ->entry[i].descriptor_count; ++j) {
         switch(templ->entry[i].descriptor_type) {
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
            assert(!(set->layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
            write_ubo_descriptor(device, cmd_buffer,
                                 set->dynamic_descriptors + dst_offset,
                                 buffer_list, src);
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            write_ubo_descriptor(device, cmd_buffer, ptr, buffer_list, src);
            break;
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            assert(!(set->layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
            write_buffer_descriptor(device, cmd_buffer,
                                    set->dynamic_descriptors + dst_offset,
                                    buffer_list, src);
            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            write_buffer_descriptor(device, cmd_buffer, ptr, buffer_list, src);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            write_texel_buffer_descriptor(device, cmd_buffer, ptr,
                                          buffer_list, *(VkBufferView *) src);
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            write_image_descriptor(device, cmd_buffer, ptr, buffer_list,
                                   templ->entry[i].descriptor_type,
                                   src);
            break;
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            write_image_descriptor(device, cmd_buffer,
                                    set->dynamic_descriptors + dst_offset,
                                    buffer_list, templ->entry[i].descriptor_type,
                                    src);
            break;
         }
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            write_combined_image_sampler_descriptor(device, cmd_buffer,
                                                    A6XX_TEX_CONST_DWORDS * 4,
                                                    ptr, buffer_list,
                                                    templ->entry[i].descriptor_type,
                                                    src,
                                                    templ->entry[i].has_sampler);
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            write_sampler_descriptor(device, ptr, src);
            break;
         default:
            unreachable("unimplemented descriptor type");
            break;
         }
         src = (char *) src + templ->entry[i].src_stride;
         ptr += templ->entry[i].dst_stride;
         dst_offset += templ->entry[i].dst_stride;
         ++buffer_list;
      }
   }
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
