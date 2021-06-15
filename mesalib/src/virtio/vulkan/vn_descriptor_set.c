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

   uint32_t max_binding = 0;
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

         if (max_binding < binding->binding)
            max_binding = binding->binding;

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
      offsetof(struct vn_descriptor_set_layout, bindings[max_binding + 1]);
   struct vn_descriptor_set_layout *layout =
      vk_zalloc(alloc, layout_size, VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!layout) {
      vk_free(alloc, local_bindings);
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   vn_object_base_init(&layout->base, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                       &dev->base);

   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding =
         &pCreateInfo->pBindings[i];
      struct vn_descriptor_set_layout_binding *dst =
         &layout->bindings[binding->binding];

      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         dst->has_immutable_samplers = binding->pImmutableSamplers;
         break;
      default:
         break;
      }
   }

   VkDescriptorSetLayout layout_handle =
      vn_descriptor_set_layout_to_handle(layout);
   vn_async_vkCreateDescriptorSetLayout(dev->instance, device, pCreateInfo,
                                        NULL, &layout_handle);

   vk_free(alloc, local_bindings);

   *pSetLayout = layout_handle;

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

   for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      struct vn_descriptor_set *set =
         vk_zalloc(alloc, sizeof(*set), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!set) {
         for (uint32_t j = 0; j < i; j++) {
            set = vn_descriptor_set_from_handle(pDescriptorSets[j]);
            list_del(&set->head);
            vk_free(alloc, set);
         }
         memset(pDescriptorSets, 0,
                sizeof(*pDescriptorSets) * pAllocateInfo->descriptorSetCount);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      vn_object_base_init(&set->base, VK_OBJECT_TYPE_DESCRIPTOR_SET,
                          &dev->base);
      set->layout =
         vn_descriptor_set_layout_from_handle(pAllocateInfo->pSetLayouts[i]);
      list_addtail(&set->head, &pool->descriptor_sets);

      VkDescriptorSet set_handle = vn_descriptor_set_to_handle(set);
      pDescriptorSets[i] = set_handle;
   }

   VkResult result = vn_call_vkAllocateDescriptorSets(
      dev->instance, device, pAllocateInfo, pDescriptorSets);
   if (result != VK_SUCCESS) {
      for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
         struct vn_descriptor_set *set =
            vn_descriptor_set_from_handle(pDescriptorSets[i]);
         list_del(&set->head);
         vk_free(alloc, set);
      }
      memset(pDescriptorSets, 0,
             sizeof(*pDescriptorSets) * pAllocateInfo->descriptorSetCount);
      return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
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
