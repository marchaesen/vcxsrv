/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "util/mesa-sha1.h"
#include "vk_alloc.h"
#include "vk_descriptor_update_template.h"
#include "vk_descriptors.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"

#include "util/bitset.h"

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_buffer_view.h"
#include "panvk_descriptor_set.h"
#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_macros.h"
#include "panvk_priv_bo.h"
#include "panvk_sampler.h"

static inline const bool
is_dynamic_buffer(VkDescriptorType type)
{
   return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
          type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

static void *
get_desc_slot_ptr(struct panvk_descriptor_set *set, uint32_t binding,
                  uint32_t elem, VkDescriptorType type)
{
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];

   uint32_t offset = panvk_get_desc_index(binding_layout, elem, type);

   assert(offset < set->layout->desc_count);

   return (char *)set->descs.host + offset * PANVK_DESCRIPTOR_SIZE;
}

#define write_desc(set, binding, elem, desc, type)                             \
   do {                                                                        \
      static_assert(sizeof(*(desc)) == PANVK_DESCRIPTOR_SIZE,                  \
                    "wrong descriptor size");                                  \
      void *__dst = get_desc_slot_ptr(set, binding, elem, type);               \
      memcpy(__dst, (desc), PANVK_DESCRIPTOR_SIZE);                            \
   } while (0)

static void
write_sampler_desc(struct panvk_descriptor_set *set,
                   const VkDescriptorImageInfo *const pImageInfo,
                   uint32_t binding, uint32_t elem, bool write_immutable)
{
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];

   if (binding_layout->immutable_samplers && !write_immutable)
      return;

   const struct mali_sampler_packed *sampler_desc;

   if (binding_layout->immutable_samplers) {
      sampler_desc = &binding_layout->immutable_samplers[elem];
   } else {
      struct panvk_sampler *sampler = panvk_sampler_from_handle(
         pImageInfo ? pImageInfo->sampler : VK_NULL_HANDLE);

      sampler_desc = sampler ? &sampler->desc : NULL;
   }

   if (sampler_desc)
      write_desc(set, binding, elem, sampler_desc, VK_DESCRIPTOR_TYPE_SAMPLER);
}

static void
write_image_view_desc(struct panvk_descriptor_set *set,
                      const VkDescriptorImageInfo *const pImageInfo,
                      uint32_t binding, uint32_t elem, VkDescriptorType type)
{
   if (pImageInfo && pImageInfo->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(panvk_image_view, view, pImageInfo->imageView);

#if PAN_ARCH <= 7
      if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
         write_desc(set, binding, elem, &view->descs.img_attrib_buf, type);
      else
         write_desc(set, binding, elem, &view->descs.tex, type);
#else
      write_desc(set, binding, elem, &view->descs.tex, type);
#endif
   }
}

static void
write_buffer_desc(struct panvk_descriptor_set *set,
                  const VkDescriptorBufferInfo *const info, uint32_t binding,
                  uint32_t elem, VkDescriptorType type)
{
   VK_FROM_HANDLE(panvk_buffer, buffer, info->buffer);
   const uint64_t range = panvk_buffer_range(buffer, info->offset, info->range);
   assert(range <= UINT32_MAX);

#if PAN_ARCH <= 7
   if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
      struct panvk_ssbo_addr desc = {
         .base_addr = panvk_buffer_gpu_ptr(buffer, info->offset),
         .size = range,
      };

      write_desc(set, binding, elem, &desc, type);
   } else {
      struct {
         struct mali_uniform_buffer_packed ubo;
         uint32_t pad[6];
      } padded_desc = {0};

      pan_pack(&padded_desc.ubo, UNIFORM_BUFFER, cfg) {
         cfg.pointer = panvk_buffer_gpu_ptr(buffer, info->offset);
         cfg.entries = DIV_ROUND_UP(range, 16);
      }

      write_desc(set, binding, elem, &padded_desc, type);
   }
#else
   struct mali_buffer_packed desc;

   pan_pack(&desc, BUFFER, cfg) {
      cfg.address = panvk_buffer_gpu_ptr(buffer, info->offset);
      cfg.size = range;
   }
   write_desc(set, binding, elem, &desc, type);
#endif
}

static void
write_dynamic_buffer_desc(struct panvk_descriptor_set *set,
                          const VkDescriptorBufferInfo *const info,
                          uint32_t binding, uint32_t elem)
{
   VK_FROM_HANDLE(panvk_buffer, buffer, info->buffer);
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];
   uint32_t dyn_buf_idx = binding_layout->desc_idx + elem;
   const uint64_t range = panvk_buffer_range(buffer, info->offset, info->range);

   assert(range <= UINT32_MAX);
   assert(dyn_buf_idx < ARRAY_SIZE(set->dyn_bufs));

   set->dyn_bufs[dyn_buf_idx].dev_addr =
      panvk_buffer_gpu_ptr(buffer, info->offset);
   set->dyn_bufs[dyn_buf_idx].size = range;
}

static void
write_buffer_view_desc(struct panvk_descriptor_set *set,
                       const VkBufferView bufferView, uint32_t binding,
                       uint32_t elem, VkDescriptorType type)
{
   if (bufferView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(panvk_buffer_view, view, bufferView);

#if PAN_ARCH <= 7
      if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
         write_desc(set, binding, elem, &view->descs.img_attrib_buf, type);
      else
         write_desc(set, binding, elem, &view->descs.tex, type);
#else
      write_desc(set, binding, elem, &view->descs.tex, type);
#endif
   }
}

static void
panvk_desc_pool_free_set(struct panvk_descriptor_pool *pool,
                         struct panvk_descriptor_set *set)
{
   uintptr_t set_idx = set - pool->sets;
   assert(set_idx < pool->max_sets);

   if (!BITSET_TEST(pool->free_sets, set_idx)) {
      if (set->desc_count)
         util_vma_heap_free(&pool->desc_heap, set->descs.dev,
                            set->desc_count * PANVK_DESCRIPTOR_SIZE);

      BITSET_SET(pool->free_sets, set_idx);

      /* Discard constness to call vk_descriptor_set_layout_unref(). */
      struct panvk_descriptor_set_layout *set_layout =
         (struct panvk_descriptor_set_layout *)set->layout;

      vk_descriptor_set_layout_unref(pool->base.device, &set_layout->vk);
      vk_object_base_finish(&set->base);
      memset(set, 0, sizeof(*set));
   }
}

static void
panvk_destroy_descriptor_pool(struct panvk_device *device,
                              const VkAllocationCallbacks *pAllocator,
                              struct panvk_descriptor_pool *pool)
{
   for (uint32_t i = 0; i < pool->max_sets; i++)
      panvk_desc_pool_free_set(pool, &pool->sets[i]);

   if (pool->desc_bo) {
      util_vma_heap_finish(&pool->desc_heap);
      panvk_priv_bo_unref(pool->desc_bo);
   }

   vk_object_free(&device->vk, pAllocator, pool);
}

VkResult
panvk_per_arch(CreateDescriptorPool)(
   VkDevice _device, const VkDescriptorPoolCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
   VK_FROM_HANDLE(panvk_device, device, _device);

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_pool, pool, 1);
   VK_MULTIALLOC_DECL(&ma, BITSET_WORD, free_sets,
                      BITSET_WORDS(pCreateInfo->maxSets));
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_set, sets,
                      pCreateInfo->maxSets);

   if (!vk_object_multizalloc(&device->vk, &ma, pAllocator,
                              VK_OBJECT_TYPE_DESCRIPTOR_POOL))
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t desc_count = 0;
   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      if (!is_dynamic_buffer(pCreateInfo->pPoolSizes[i].type))
         desc_count += panvk_get_desc_stride(pCreateInfo->pPoolSizes[i].type) *
                       pCreateInfo->pPoolSizes[i].descriptorCount;
   }

   /* initialize to all ones to indicate all sets are free */
   BITSET_SET_RANGE(free_sets, 0, pCreateInfo->maxSets - 1);
   pool->free_sets = free_sets;
   pool->sets = sets;
   pool->max_sets = pCreateInfo->maxSets;

   if (desc_count) {
      /* adjust desc_count to account for 1 dummy sampler per descriptor set */
      desc_count += pool->max_sets;

      uint64_t pool_size = desc_count * PANVK_DESCRIPTOR_SIZE;
      VkResult result = panvk_priv_bo_create(device, pool_size, 0,
                                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                             &pool->desc_bo);
      if (result != VK_SUCCESS) {
         panvk_destroy_descriptor_pool(device, pAllocator, pool);
         return result;
      }
      uint64_t bo_size = pool->desc_bo->bo->size;
      assert(pool_size <= bo_size);
      util_vma_heap_init(&pool->desc_heap, pool->desc_bo->addr.dev, bo_size);
   }

   *pDescriptorPool = panvk_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

void
panvk_per_arch(DestroyDescriptorPool)(VkDevice _device, VkDescriptorPool _pool,
                                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, _pool);

   if (pool)
      panvk_destroy_descriptor_pool(device, pAllocator, pool);
}

static void
desc_set_write_immutable_samplers(struct panvk_descriptor_set *set,
                                  uint32_t variable_count)
{
   const struct panvk_descriptor_set_layout *layout = set->layout;

   for (uint32_t b = 0; b < layout->binding_count; b++) {
      if (layout->bindings[b].type != VK_DESCRIPTOR_TYPE_SAMPLER &&
          layout->bindings[b].type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
         continue;

      if (layout->bindings[b].immutable_samplers == NULL)
         continue;

      uint32_t array_size = layout->bindings[b].desc_count;

      if (layout->bindings[b].flags &
          VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)
         array_size = variable_count;

      for (uint32_t j = 0; j < array_size; j++) {
         write_desc(set, b, j, &layout->bindings[b].immutable_samplers[j],
                    VK_DESCRIPTOR_TYPE_SAMPLER);
      }
   }
}

static VkResult
panvk_desc_pool_allocate_set(struct panvk_descriptor_pool *pool,
                             struct panvk_descriptor_set_layout *layout,
                             uint32_t variable_count,
                             struct panvk_descriptor_set **out)
{
   uint32_t num_descs = layout->desc_count;

   if (layout->binding_count) {
      uint32_t last_binding = layout->binding_count - 1;

      if ((layout->bindings[last_binding].flags &
           VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) &&
          !is_dynamic_buffer(layout->bindings[last_binding].type)) {
         uint32_t desc_stride =
            panvk_get_desc_stride(layout->bindings[last_binding].type);

         num_descs -= layout->bindings[last_binding].desc_count * desc_stride;
         num_descs += variable_count * desc_stride;
      }
   }

   uint64_t descs_size = num_descs * PANVK_DESCRIPTOR_SIZE;
   uint32_t first_free_set =
      __bitset_ffs(pool->free_sets, BITSET_WORDS(pool->max_sets));
   if (first_free_set == 0 || pool->desc_heap.free_size < descs_size)
      return panvk_error(pool, VK_ERROR_OUT_OF_POOL_MEMORY);

   uint64_t descs_dev_addr = 0;
   if (num_descs) {
      descs_dev_addr = util_vma_heap_alloc(&pool->desc_heap, descs_size,
                                           PANVK_DESCRIPTOR_SIZE);
      if (!descs_dev_addr)
         return panvk_error(pool, VK_ERROR_FRAGMENTED_POOL);
   }
   struct panvk_descriptor_set *set = &pool->sets[first_free_set - 1];

   vk_object_base_init(pool->base.device, &set->base,
                       VK_OBJECT_TYPE_DESCRIPTOR_SET);
   vk_descriptor_set_layout_ref(&layout->vk);
   set->layout = layout;
   set->desc_count = num_descs;
   if (pool->desc_bo) {
      set->descs.dev = descs_dev_addr;
      set->descs.host =
         pool->desc_bo->addr.host + set->descs.dev - pool->desc_bo->addr.dev;
   }
   desc_set_write_immutable_samplers(set, variable_count);
   BITSET_CLEAR(pool->free_sets, first_free_set - 1);

   *out = set;
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(AllocateDescriptorSets)(
   VkDevice _device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
   VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, pAllocateInfo->descriptorPool);
   VkResult result = VK_SUCCESS;
   unsigned i;

   struct panvk_descriptor_set *set = NULL;

   const VkDescriptorSetVariableDescriptorCountAllocateInfo *var_desc_count =
      vk_find_struct_const(
         pAllocateInfo->pNext,
         DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);

   /* allocate a set of buffers for each shader to contain descriptors */
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set_layout, layout,
                     pAllocateInfo->pSetLayouts[i]);
      /* If descriptorSetCount is zero or this structure is not included in
       * the pNext chain, then the variable lengths are considered to be zero.
       */
      const uint32_t variable_count =
         var_desc_count && var_desc_count->descriptorSetCount > 0
            ? var_desc_count->pDescriptorCounts[i]
            : 0;

      result = panvk_desc_pool_allocate_set(pool, layout, variable_count, &set);
      if (result != VK_SUCCESS)
         goto err_free_sets;

      pDescriptorSets[i] = panvk_descriptor_set_to_handle(set);
   }

   return VK_SUCCESS;

err_free_sets:
   panvk_per_arch(FreeDescriptorSets)(_device, pAllocateInfo->descriptorPool, i,
                                      pDescriptorSets);
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++)
      pDescriptorSets[i] = VK_NULL_HANDLE;

   return result;
}

VkResult
panvk_per_arch(FreeDescriptorSets)(VkDevice _device,
                                   VkDescriptorPool descriptorPool,
                                   uint32_t descriptorSetCount,
                                   const VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, descriptorPool);

   for (unsigned i = 0; i < descriptorSetCount; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set, set, pDescriptorSets[i]);

      if (set)
         panvk_desc_pool_free_set(pool, set);
   }
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(ResetDescriptorPool)(VkDevice _device, VkDescriptorPool _pool,
                                    VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, _pool);

   for (uint32_t i = 0; i < pool->max_sets; i++)
      panvk_desc_pool_free_set(pool, &pool->sets[i]);

   BITSET_SET_RANGE(pool->free_sets, 0, pool->max_sets - 1);
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(descriptor_set_write)(struct panvk_descriptor_set *set,
                                     const VkWriteDescriptorSet *write,
                                     bool write_immutable_samplers)
{
   switch (write->descriptorType) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_sampler_desc(set, write->pImageInfo + j, write->dstBinding,
                            write->dstArrayElement + j,
                            write_immutable_samplers);
      }
      break;

   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_sampler_desc(set, write->pImageInfo + j, write->dstBinding,
                            write->dstArrayElement + j,
                            write_immutable_samplers);
         write_image_view_desc(set, write->pImageInfo + j, write->dstBinding,
                               write->dstArrayElement + j,
                               VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      }
      break;

   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_image_view_desc(set, write->pImageInfo + j, write->dstBinding,
                               write->dstArrayElement + j,
                               write->descriptorType);
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_buffer_view_desc(set, write->pTexelBufferView[j],
                                write->dstBinding, write->dstArrayElement + j,
                                write->descriptorType);
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_buffer_desc(set, write->pBufferInfo + j, write->dstBinding,
                           write->dstArrayElement + j, write->descriptorType);
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_dynamic_buffer_desc(set, write->pBufferInfo + j,
                                   write->dstBinding,
                                   write->dstArrayElement + j);
      }
      break;

   default:
      unreachable("Unsupported descriptor type");
   }
   return VK_SUCCESS;
}

static VkResult
panvk_descriptor_set_copy(const VkCopyDescriptorSet *copy)
{
   VK_FROM_HANDLE(panvk_descriptor_set, src_set, copy->srcSet);
   VK_FROM_HANDLE(panvk_descriptor_set, dst_set, copy->dstSet);

   const struct panvk_descriptor_set_binding_layout *dst_binding_layout =
      &dst_set->layout->bindings[copy->dstBinding];
   const struct panvk_descriptor_set_binding_layout *src_binding_layout =
      &src_set->layout->bindings[copy->srcBinding];

   assert(dst_binding_layout->type == src_binding_layout->type);

   switch (src_binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      for (uint32_t i = 0; i < copy->descriptorCount; i++) {
         void *dst = get_desc_slot_ptr(dst_set, copy->dstBinding,
                                       copy->dstArrayElement + i,
                                       dst_binding_layout->type);
         const void *src = get_desc_slot_ptr(src_set, copy->srcBinding,
                                             copy->srcArrayElement + i,
                                             src_binding_layout->type);

         memcpy(dst, src,
                PANVK_DESCRIPTOR_SIZE *
                   panvk_get_desc_stride(src_binding_layout->type));
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      uint32_t dst_dyn_buf_idx =
         dst_binding_layout->desc_idx + copy->dstArrayElement;
      uint32_t src_dyn_buf_idx =
         src_binding_layout->desc_idx + copy->srcArrayElement;

      memcpy(
         &dst_set->dyn_bufs[dst_dyn_buf_idx],
         &src_set->dyn_bufs[src_dyn_buf_idx],
         copy->descriptorCount * sizeof(dst_set->dyn_bufs[dst_dyn_buf_idx]));
      break;
   }

   default:
      unreachable("Unsupported descriptor type");
   }

   return VK_SUCCESS;
}

void
panvk_per_arch(UpdateDescriptorSets)(
   VkDevice _device, uint32_t descriptorWriteCount,
   const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
   const VkCopyDescriptorSet *pDescriptorCopies)
{
   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set, set, pDescriptorWrites[i].dstSet);

      panvk_per_arch(descriptor_set_write)(set, &pDescriptorWrites[i], false);
   }

   for (uint32_t i = 0; i < descriptorCopyCount; i++)
      panvk_descriptor_set_copy(&pDescriptorCopies[i]);
}

void
panvk_per_arch(descriptor_set_write_template)(
   struct panvk_descriptor_set *set,
   const struct vk_descriptor_update_template *template, const void *data,
   bool write_immutable_samplers)
{
   for (uint32_t i = 0; i < template->entry_count; i++) {
      const struct vk_descriptor_template_entry *entry = &template->entries[i];

      switch (entry->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;

            write_sampler_desc(set, info, entry->binding,
                               entry->array_element + j,
                               write_immutable_samplers);
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;
            write_sampler_desc(set, info, entry->binding,
                               entry->array_element + j,
                               write_immutable_samplers);
            write_image_view_desc(set, info, entry->binding,
                                  entry->array_element + j,
                                  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;

            write_image_view_desc(set, info, entry->binding,
                                  entry->array_element + j, entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkBufferView *bview =
               data + entry->offset + j * entry->stride;

            write_buffer_view_desc(set, *bview, entry->binding,
                                   entry->array_element + j, entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_buffer_desc(set, info, entry->binding,
                              entry->array_element + j, entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_dynamic_buffer_desc(set, info, entry->binding,
                                      entry->array_element + j);
         }
         break;
      default:
         unreachable("Unsupported descriptor type");
      }
   }
}

void
panvk_per_arch(UpdateDescriptorSetWithTemplate)(
   VkDevice _device, VkDescriptorSet descriptorSet,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData)
{
   VK_FROM_HANDLE(panvk_descriptor_set, set, descriptorSet);
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  descriptorUpdateTemplate);

   panvk_per_arch(descriptor_set_write_template)(set, template, pData, false);
}
