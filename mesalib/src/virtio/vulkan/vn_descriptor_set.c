/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_descriptor_set.h"

#include "venus-protocol/vn_protocol_driver_descriptor_pool.h"
#include "venus-protocol/vn_protocol_driver_descriptor_set.h"
#include "venus-protocol/vn_protocol_driver_descriptor_set_layout.h"
#include "venus-protocol/vn_protocol_driver_descriptor_update_template.h"

#include "vn_device.h"

/* descriptor set layout commands */

void
vn_GetDescriptorSetLayoutSupport(
   VkDevice device,
   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   VkDescriptorSetLayoutSupport *pSupport)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO per-device cache */
   vn_call_vkGetDescriptorSetLayoutSupport(dev->instance, device, pCreateInfo,
                                           pSupport);
}

static void
vn_descriptor_set_layout_init(
   struct vn_device *dev,
   const VkDescriptorSetLayoutCreateInfo *create_info,
   uint32_t last_binding,
   struct vn_descriptor_set_layout *layout)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkDescriptorSetLayout layout_handle =
      vn_descriptor_set_layout_to_handle(layout);
   const VkDescriptorSetLayoutBindingFlagsCreateInfo *binding_flags =
      vk_find_struct_const(create_info->pNext,
                           DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);

   /* 14.2.1. Descriptor Set Layout
    *
    * If bindingCount is zero or if this structure is not included in
    * the pNext chain, the VkDescriptorBindingFlags for each descriptor
    * set layout binding is considered to be zero.
    */
   if (binding_flags && !binding_flags->bindingCount)
      binding_flags = NULL;

   layout->last_binding = last_binding;

   for (uint32_t i = 0; i < create_info->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding_info =
         &create_info->pBindings[i];
      struct vn_descriptor_set_layout_binding *binding =
         &layout->bindings[binding_info->binding];

      if (binding_info->binding == last_binding) {
         /* 14.2.1. Descriptor Set Layout
          *
          * VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT must only be
          * used for the last binding in the descriptor set layout (i.e. the
          * binding with the largest value of binding).
          *
          * 41. Features
          *
          * descriptorBindingVariableDescriptorCount indicates whether the
          * implementation supports descriptor sets with a variable-sized last
          * binding. If this feature is not enabled,
          * VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT must not be
          * used.
          */
         layout->has_variable_descriptor_count =
            binding_flags &&
            (binding_flags->pBindingFlags[i] &
             VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT);
      }

      binding->type = binding_info->descriptorType;
      binding->count = binding_info->descriptorCount;

      switch (binding_info->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         binding->has_immutable_samplers = binding_info->pImmutableSamplers;
         break;
      default:
         break;
      }
   }

   vn_async_vkCreateDescriptorSetLayout(dev->instance, dev_handle,
                                        create_info, NULL, &layout_handle);
}

VkResult
vn_CreateDescriptorSetLayout(
   VkDevice device,
   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDescriptorSetLayout *pSetLayout)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   uint32_t last_binding = 0;
   VkDescriptorSetLayoutBinding *local_bindings = NULL;
   VkDescriptorSetLayoutCreateInfo local_create_info;
   if (pCreateInfo->bindingCount) {
      /* the encoder does not ignore
       * VkDescriptorSetLayoutBinding::pImmutableSamplers when it should
       */
      const size_t binding_size =
         sizeof(*pCreateInfo->pBindings) * pCreateInfo->bindingCount;
      local_bindings = vk_alloc(alloc, binding_size, VN_DEFAULT_ALIGN,
                                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!local_bindings)
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      memcpy(local_bindings, pCreateInfo->pBindings, binding_size);
      for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
         VkDescriptorSetLayoutBinding *binding = &local_bindings[i];

         if (last_binding < binding->binding)
            last_binding = binding->binding;

         switch (binding->descriptorType) {
         case VK_DESCRIPTOR_TYPE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            break;
         default:
            binding->pImmutableSamplers = NULL;
            break;
         }
      }

      local_create_info = *pCreateInfo;
      local_create_info.pBindings = local_bindings;
      pCreateInfo = &local_create_info;
   }

   const size_t layout_size =
      offsetof(struct vn_descriptor_set_layout, bindings[last_binding + 1]);
   struct vn_descriptor_set_layout *layout =
      vk_zalloc(alloc, layout_size, VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!layout) {
      vk_free(alloc, local_bindings);
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   vn_object_base_init(&layout->base, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                       &dev->base);

   vn_descriptor_set_layout_init(dev, pCreateInfo, last_binding, layout);

   vk_free(alloc, local_bindings);

   *pSetLayout = vn_descriptor_set_layout_to_handle(layout);

   return VK_SUCCESS;
}

void
vn_DestroyDescriptorSetLayout(VkDevice device,
                              VkDescriptorSetLayout descriptorSetLayout,
                              const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_descriptor_set_layout *layout =
      vn_descriptor_set_layout_from_handle(descriptorSetLayout);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!layout)
      return;

   vn_async_vkDestroyDescriptorSetLayout(dev->instance, device,
                                         descriptorSetLayout, NULL);

   vn_object_base_fini(&layout->base);
   vk_free(alloc, layout);
}

/* descriptor pool commands */

VkResult
vn_CreateDescriptorPool(VkDevice device,
                        const VkDescriptorPoolCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkDescriptorPool *pDescriptorPool)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_descriptor_pool *pool =
      vk_zalloc(alloc, sizeof(*pool), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&pool->base, VK_OBJECT_TYPE_DESCRIPTOR_POOL,
                       &dev->base);

   pool->allocator = *alloc;

   /* Without VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, the set
    * allocation must not fail due to a fragmented pool per spec. In this
    * case, set allocation can be asynchronous with pool resource tracking.
    */
   pool->async_set_allocation = !(
      pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

   pool->max.set_count = pCreateInfo->maxSets;

   for (uint32_t i = 0; i < pCreateInfo->poolSizeCount; i++) {
      const VkDescriptorPoolSize *pool_size = &pCreateInfo->pPoolSizes[i];

      assert(pool_size->type < VN_NUM_DESCRIPTOR_TYPES);

      pool->max.descriptor_counts[pool_size->type] +=
         pool_size->descriptorCount;
   }

   list_inithead(&pool->descriptor_sets);

   VkDescriptorPool pool_handle = vn_descriptor_pool_to_handle(pool);
   vn_async_vkCreateDescriptorPool(dev->instance, device, pCreateInfo, NULL,
                                   &pool_handle);

   *pDescriptorPool = pool_handle;

   return VK_SUCCESS;
}

void
vn_DestroyDescriptorPool(VkDevice device,
                         VkDescriptorPool descriptorPool,
                         const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_descriptor_pool *pool =
      vn_descriptor_pool_from_handle(descriptorPool);
   const VkAllocationCallbacks *alloc;

   if (!pool)
      return;

   alloc = pAllocator ? pAllocator : &pool->allocator;

   /* We must emit vkDestroyDescriptorPool before freeing the sets in
    * pool->descriptor_sets.  Otherwise, another thread might reuse their
    * object ids while they still refer to the sets in the renderer.
    */
   vn_async_vkDestroyDescriptorPool(dev->instance, device, descriptorPool,
                                    NULL);

   list_for_each_entry_safe(struct vn_descriptor_set, set,
                            &pool->descriptor_sets, head) {
      list_del(&set->head);

      vn_object_base_fini(&set->base);
      vk_free(alloc, set);
   }

   vn_object_base_fini(&pool->base);
   vk_free(alloc, pool);
}

static bool
vn_descriptor_pool_alloc_descriptors(
   struct vn_descriptor_pool *pool,
   const struct vn_descriptor_set_layout *layout,
   uint32_t last_binding_descriptor_count)
{
   struct vn_descriptor_pool_state recovery;

   if (!pool->async_set_allocation)
      return true;

   if (pool->used.set_count == pool->max.set_count)
      return false;

   /* backup current pool state to recovery */
   recovery = pool->used;

   ++pool->used.set_count;

   for (uint32_t i = 0; i <= layout->last_binding; i++) {
      const VkDescriptorType type = layout->bindings[i].type;
      const uint32_t count = i == layout->last_binding
                                ? last_binding_descriptor_count
                                : layout->bindings[i].count;

      pool->used.descriptor_counts[type] += count;

      if (pool->used.descriptor_counts[type] >
          pool->max.descriptor_counts[type]) {
         /* restore pool state before this allocation */
         pool->used = recovery;
         return false;
      }
   }

   return true;
}

static void
vn_descriptor_pool_free_descriptors(
   struct vn_descriptor_pool *pool,
   const struct vn_descriptor_set_layout *layout,
   uint32_t last_binding_descriptor_count)
{
   if (!pool->async_set_allocation)
      return;

   for (uint32_t i = 0; i <= layout->last_binding; i++) {
      const uint32_t count = i == layout->last_binding
                                ? last_binding_descriptor_count
                                : layout->bindings[i].count;

      pool->used.descriptor_counts[layout->bindings[i].type] -= count;
   }

   --pool->used.set_count;
}

static void
vn_descriptor_pool_reset_descriptors(struct vn_descriptor_pool *pool)
{
   if (!pool->async_set_allocation)
      return;

   memset(&pool->used, 0, sizeof(pool->used));
}

VkResult
vn_ResetDescriptorPool(VkDevice device,
                       VkDescriptorPool descriptorPool,
                       VkDescriptorPoolResetFlags flags)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_descriptor_pool *pool =
      vn_descriptor_pool_from_handle(descriptorPool);
   const VkAllocationCallbacks *alloc = &pool->allocator;

   vn_async_vkResetDescriptorPool(dev->instance, device, descriptorPool,
                                  flags);

   list_for_each_entry_safe(struct vn_descriptor_set, set,
                            &pool->descriptor_sets, head) {
      list_del(&set->head);

      vn_object_base_fini(&set->base);
      vk_free(alloc, set);
   }

   vn_descriptor_pool_reset_descriptors(pool);

   return VK_SUCCESS;
}

/* descriptor set commands */

VkResult
vn_AllocateDescriptorSets(VkDevice device,
                          const VkDescriptorSetAllocateInfo *pAllocateInfo,
                          VkDescriptorSet *pDescriptorSets)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_descriptor_pool *pool =
      vn_descriptor_pool_from_handle(pAllocateInfo->descriptorPool);
   const VkAllocationCallbacks *alloc = &pool->allocator;
   const VkDescriptorSetVariableDescriptorCountAllocateInfo *variable_info =
      NULL;
   VkResult result;

   /* 14.2.3. Allocation of Descriptor Sets
    *
    * If descriptorSetCount is zero or this structure is not included in
    * the pNext chain, then the variable lengths are considered to be zero.
    */
   variable_info = vk_find_struct_const(
      pAllocateInfo->pNext,
      DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);

   if (variable_info && !variable_info->descriptorSetCount)
      variable_info = NULL;

   for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      const struct vn_descriptor_set_layout *layout =
         vn_descriptor_set_layout_from_handle(pAllocateInfo->pSetLayouts[i]);
      uint32_t last_binding_descriptor_count = 0;
      struct vn_descriptor_set *set = NULL;

      /* 14.2.3. Allocation of Descriptor Sets
       *
       * If VkDescriptorSetAllocateInfo::pSetLayouts[i] does not include a
       * variable count descriptor binding, then pDescriptorCounts[i] is
       * ignored.
       */
      if (!layout->has_variable_descriptor_count) {
         last_binding_descriptor_count =
            layout->bindings[layout->last_binding].count;
      } else if (variable_info) {
         last_binding_descriptor_count = variable_info->pDescriptorCounts[i];
      }

      if (!vn_descriptor_pool_alloc_descriptors(
             pool, layout, last_binding_descriptor_count)) {
         pDescriptorSets[i] = VK_NULL_HANDLE;
         result = VK_ERROR_OUT_OF_POOL_MEMORY;
         goto fail;
      }

      set = vk_zalloc(alloc, sizeof(*set), VN_DEFAULT_ALIGN,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!set) {
         vn_descriptor_pool_free_descriptors(pool, layout,
                                             last_binding_descriptor_count);
         pDescriptorSets[i] = VK_NULL_HANDLE;
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      vn_object_base_init(&set->base, VK_OBJECT_TYPE_DESCRIPTOR_SET,
                          &dev->base);

      set->layout = layout;
      set->last_binding_descriptor_count = last_binding_descriptor_count;
      list_addtail(&set->head, &pool->descriptor_sets);

      VkDescriptorSet set_handle = vn_descriptor_set_to_handle(set);
      pDescriptorSets[i] = set_handle;
   }

   if (pool->async_set_allocation) {
      vn_async_vkAllocateDescriptorSets(dev->instance, device, pAllocateInfo,
                                        pDescriptorSets);
   } else {
      result = vn_call_vkAllocateDescriptorSets(
         dev->instance, device, pAllocateInfo, pDescriptorSets);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;

fail:
   for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      struct vn_descriptor_set *set =
         vn_descriptor_set_from_handle(pDescriptorSets[i]);
      if (!set)
         break;

      vn_descriptor_pool_free_descriptors(pool, set->layout,
                                          set->last_binding_descriptor_count);
      list_del(&set->head);
      vn_object_base_fini(&set->base);
      vk_free(alloc, set);
   }

   memset(pDescriptorSets, 0,
          sizeof(*pDescriptorSets) * pAllocateInfo->descriptorSetCount);

   return vn_error(dev->instance, result);
}

VkResult
vn_FreeDescriptorSets(VkDevice device,
                      VkDescriptorPool descriptorPool,
                      uint32_t descriptorSetCount,
                      const VkDescriptorSet *pDescriptorSets)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_descriptor_pool *pool =
      vn_descriptor_pool_from_handle(descriptorPool);
   const VkAllocationCallbacks *alloc = &pool->allocator;

   vn_async_vkFreeDescriptorSets(dev->instance, device, descriptorPool,
                                 descriptorSetCount, pDescriptorSets);

   for (uint32_t i = 0; i < descriptorSetCount; i++) {
      struct vn_descriptor_set *set =
         vn_descriptor_set_from_handle(pDescriptorSets[i]);

      if (!set)
         continue;

      list_del(&set->head);

      vn_object_base_fini(&set->base);
      vk_free(alloc, set);
   }

   return VK_SUCCESS;
}

static struct vn_update_descriptor_sets *
vn_update_descriptor_sets_alloc(uint32_t write_count,
                                uint32_t image_count,
                                uint32_t buffer_count,
                                uint32_t view_count,
                                const VkAllocationCallbacks *alloc,
                                VkSystemAllocationScope scope)
{
   const size_t writes_offset = sizeof(struct vn_update_descriptor_sets);
   const size_t images_offset =
      writes_offset + sizeof(VkWriteDescriptorSet) * write_count;
   const size_t buffers_offset =
      images_offset + sizeof(VkDescriptorImageInfo) * image_count;
   const size_t views_offset =
      buffers_offset + sizeof(VkDescriptorBufferInfo) * buffer_count;
   const size_t alloc_size = views_offset + sizeof(VkBufferView) * view_count;

   void *storage = vk_alloc(alloc, alloc_size, VN_DEFAULT_ALIGN, scope);
   if (!storage)
      return NULL;

   struct vn_update_descriptor_sets *update = storage;
   update->write_count = write_count;
   update->writes = storage + writes_offset;
   update->images = storage + images_offset;
   update->buffers = storage + buffers_offset;
   update->views = storage + views_offset;

   return update;
}

static struct vn_update_descriptor_sets *
vn_update_descriptor_sets_parse_writes(uint32_t write_count,
                                       const VkWriteDescriptorSet *writes,
                                       const VkAllocationCallbacks *alloc)
{
   uint32_t img_count = 0;
   for (uint32_t i = 0; i < write_count; i++) {
      const VkWriteDescriptorSet *write = &writes[i];
      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         img_count += write->descriptorCount;
         break;
      default:
         break;
      }
   }

   struct vn_update_descriptor_sets *update =
      vn_update_descriptor_sets_alloc(write_count, img_count, 0, 0, alloc,
                                      VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!update)
      return NULL;

   /* the encoder does not ignore
    * VkWriteDescriptorSet::{pImageInfo,pBufferInfo,pTexelBufferView} when it
    * should
    *
    * TODO make the encoder smarter
    */
   memcpy(update->writes, writes, sizeof(*writes) * write_count);
   img_count = 0;
   for (uint32_t i = 0; i < write_count; i++) {
      const struct vn_descriptor_set *set =
         vn_descriptor_set_from_handle(writes[i].dstSet);
      const struct vn_descriptor_set_layout_binding *binding =
         &set->layout->bindings[writes[i].dstBinding];
      VkWriteDescriptorSet *write = &update->writes[i];
      VkDescriptorImageInfo *imgs = &update->images[img_count];

      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         memcpy(imgs, write->pImageInfo,
                sizeof(*imgs) * write->descriptorCount);
         img_count += write->descriptorCount;

         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            switch (write->descriptorType) {
            case VK_DESCRIPTOR_TYPE_SAMPLER:
               imgs[j].imageView = VK_NULL_HANDLE;
               break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
               if (binding->has_immutable_samplers)
                  imgs[j].sampler = VK_NULL_HANDLE;
               break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
               imgs[j].sampler = VK_NULL_HANDLE;
               break;
            default:
               break;
            }
         }

         write->pImageInfo = imgs;
         write->pBufferInfo = NULL;
         write->pTexelBufferView = NULL;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         write->pImageInfo = NULL;
         write->pBufferInfo = NULL;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         write->pImageInfo = NULL;
         write->pTexelBufferView = NULL;
         break;
      default:
         write->pImageInfo = NULL;
         write->pBufferInfo = NULL;
         write->pTexelBufferView = NULL;
         break;
      }
   }

   return update;
}

void
vn_UpdateDescriptorSets(VkDevice device,
                        uint32_t descriptorWriteCount,
                        const VkWriteDescriptorSet *pDescriptorWrites,
                        uint32_t descriptorCopyCount,
                        const VkCopyDescriptorSet *pDescriptorCopies)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   struct vn_update_descriptor_sets *update =
      vn_update_descriptor_sets_parse_writes(descriptorWriteCount,
                                             pDescriptorWrites, alloc);
   if (!update) {
      /* TODO update one-by-one? */
      vn_log(dev->instance, "TODO descriptor set update ignored due to OOM");
      return;
   }

   vn_async_vkUpdateDescriptorSets(dev->instance, device, update->write_count,
                                   update->writes, descriptorCopyCount,
                                   pDescriptorCopies);

   vk_free(alloc, update);
}

/* descriptor update template commands */

static struct vn_update_descriptor_sets *
vn_update_descriptor_sets_parse_template(
   const VkDescriptorUpdateTemplateCreateInfo *create_info,
   const VkAllocationCallbacks *alloc,
   struct vn_descriptor_update_template_entry *entries)
{
   uint32_t img_count = 0;
   uint32_t buf_count = 0;
   uint32_t view_count = 0;
   for (uint32_t i = 0; i < create_info->descriptorUpdateEntryCount; i++) {
      const VkDescriptorUpdateTemplateEntry *entry =
         &create_info->pDescriptorUpdateEntries[i];

      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         img_count += entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         view_count += entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         buf_count += entry->descriptorCount;
         break;
      default:
         unreachable("unhandled descriptor type");
         break;
      }
   }

   struct vn_update_descriptor_sets *update = vn_update_descriptor_sets_alloc(
      create_info->descriptorUpdateEntryCount, img_count, buf_count,
      view_count, alloc, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!update)
      return NULL;

   img_count = 0;
   buf_count = 0;
   view_count = 0;
   for (uint32_t i = 0; i < create_info->descriptorUpdateEntryCount; i++) {
      const VkDescriptorUpdateTemplateEntry *entry =
         &create_info->pDescriptorUpdateEntries[i];
      VkWriteDescriptorSet *write = &update->writes[i];

      write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write->pNext = NULL;
      write->dstBinding = entry->dstBinding;
      write->dstArrayElement = entry->dstArrayElement;
      write->descriptorCount = entry->descriptorCount;
      write->descriptorType = entry->descriptorType;

      entries[i].offset = entry->offset;
      entries[i].stride = entry->stride;

      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         write->pImageInfo = &update->images[img_count];
         write->pBufferInfo = NULL;
         write->pTexelBufferView = NULL;
         img_count += entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         write->pImageInfo = NULL;
         write->pBufferInfo = NULL;
         write->pTexelBufferView = &update->views[view_count];
         view_count += entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         write->pImageInfo = NULL;
         write->pBufferInfo = &update->buffers[buf_count];
         write->pTexelBufferView = NULL;
         buf_count += entry->descriptorCount;
         break;
      default:
         break;
      }
   }

   return update;
}

VkResult
vn_CreateDescriptorUpdateTemplate(
   VkDevice device,
   const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   const size_t templ_size =
      offsetof(struct vn_descriptor_update_template,
               entries[pCreateInfo->descriptorUpdateEntryCount + 1]);
   struct vn_descriptor_update_template *templ = vk_zalloc(
      alloc, templ_size, VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!templ)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&templ->base,
                       VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE, &dev->base);

   templ->update = vn_update_descriptor_sets_parse_template(
      pCreateInfo, alloc, templ->entries);
   if (!templ->update) {
      vk_free(alloc, templ);
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   mtx_init(&templ->mutex, mtx_plain);

   /* no host object */
   VkDescriptorUpdateTemplate templ_handle =
      vn_descriptor_update_template_to_handle(templ);
   *pDescriptorUpdateTemplate = templ_handle;

   return VK_SUCCESS;
}

void
vn_DestroyDescriptorUpdateTemplate(
   VkDevice device,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_descriptor_update_template *templ =
      vn_descriptor_update_template_from_handle(descriptorUpdateTemplate);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!templ)
      return;

   /* no host object */
   vk_free(alloc, templ->update);
   mtx_destroy(&templ->mutex);

   vn_object_base_fini(&templ->base);
   vk_free(alloc, templ);
}

void
vn_UpdateDescriptorSetWithTemplate(
   VkDevice device,
   VkDescriptorSet descriptorSet,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_descriptor_set *set =
      vn_descriptor_set_from_handle(descriptorSet);
   struct vn_descriptor_update_template *templ =
      vn_descriptor_update_template_from_handle(descriptorUpdateTemplate);
   struct vn_update_descriptor_sets *update = templ->update;

   /* duplicate update instead to avoid locking? */
   mtx_lock(&templ->mutex);

   for (uint32_t i = 0; i < update->write_count; i++) {
      const struct vn_descriptor_update_template_entry *entry =
         &templ->entries[i];
      const struct vn_descriptor_set_layout_binding *binding =
         &set->layout->bindings[update->writes[i].dstBinding];
      VkWriteDescriptorSet *write = &update->writes[i];

      write->dstSet = vn_descriptor_set_to_handle(set);

      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            const bool need_sampler =
               (write->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                write->descriptorType ==
                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
               !binding->has_immutable_samplers;
            const bool need_view =
               write->descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER;
            const VkDescriptorImageInfo *src =
               pData + entry->offset + entry->stride * j;
            VkDescriptorImageInfo *dst =
               (VkDescriptorImageInfo *)&write->pImageInfo[j];

            dst->sampler = need_sampler ? src->sampler : VK_NULL_HANDLE;
            dst->imageView = need_view ? src->imageView : VK_NULL_HANDLE;
            dst->imageLayout = src->imageLayout;
         }
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            const VkBufferView *src =
               pData + entry->offset + entry->stride * j;
            VkBufferView *dst = (VkBufferView *)&write->pTexelBufferView[j];
            *dst = *src;
         }
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            const VkDescriptorBufferInfo *src =
               pData + entry->offset + entry->stride * j;
            VkDescriptorBufferInfo *dst =
               (VkDescriptorBufferInfo *)&write->pBufferInfo[j];
            *dst = *src;
         }
         break;
      default:
         unreachable("unhandled descriptor type");
         break;
      }
   }

   vn_async_vkUpdateDescriptorSets(dev->instance, device, update->write_count,
                                   update->writes, 0, NULL);

   mtx_unlock(&templ->mutex);
}
